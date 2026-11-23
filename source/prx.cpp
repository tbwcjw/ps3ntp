#include "stdafx.h"

#include <stdio.h>
#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <cell/fs/cell_fs_file_api.h>

#include <sys/time.h>
#include <sys/timer.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "stdc.h"
#include "sysnet.h"
#include "vshmain.h"
#include "vshtask.h"
#include "paf.h"

SYS_MODULE_INFO( ps3ntp, 0, 1, 0);
SYS_MODULE_START(ps3ntp_entry);
SYS_MODULE_STOP(ps3ntp_stop);
SYS_MODULE_EXIT(ps3ntp_stop);


#define NONE		-1
#define UINT32_MAX 0xFFFFFFFFUL
#define NTP_TIMESTAMP_DELTA				2208988800ULL   
#define BASE_PATH						"/dev_hdd0/tmp/ps3ntp/"


//identity
#define PS3NTPVERSION					"V1.0"
#define PS3NTPTITLE						"PS3NTP "
#define PS3NTPAUTHOR					"github:tbwcjw/ps3ntp"

//syscalls
#define SC_RING_BUZZER					392
#define SC_STOP_PRX_MODULE				482
#define SC_GET_PRX_MODULE_BY_ADDRESS	461
#define SC_PPU_THREAD_EXIT				41
#define SC_GET_CURRENT_TIME				145
#define SC_SET_CURRENT_TIME				146
#define SC_GET_TIMEBASE_FRQ				147

#define XMB_CHECK_INTERVAL_S			60

//log
#define LOG_FN							"ps3ntp.log"
#define LOG_FILE						BASE_PATH LOG_FN

//serverlist
#define SL_FN							"serverlist.txt"
#define SL_FILE							BASE_PATH SL_FN
#define NTP_MAX_SERVERS					16
#define NTP_SERVER_LEN					128
#define NTP_DEFAULT_SERVER				"pool.ntp.org"
#define SL_FILE_BUF_SIZE				(NTP_MAX_SERVERS * NTP_SERVER_LEN)

//config
#define CFG_FN							"ps3ntp.cfg"
#define CFG_FILE						BASE_PATH CFG_FN

typedef struct
{
	uint32_t ntp_poll_interval_s;
	int64_t ntp_socket_timeout_s;
	uint32_t no_answer_interval_s;
	int64_t drift_tolerance_ns;
	bool log_enable;
	bool log_persist;
	bool notify_enable;
} ntp_config_t;

static const ntp_config_t ntp_config_default = {
	1020,         // ntp_poll_interval_s
	30,           // ntp_socket_timeout_s
	60,           // no_answer_interval_s
	100000000LL,  // drift_tolerance_ns (100 ms)
	1,            // log_enable
	1,            // log_persist
	1,            // notify_enable
};

static ntp_config_t ntp_config;

static char server_buf[NTP_MAX_SERVERS][NTP_SERVER_LEN];
static const char *ntp_servers[NTP_MAX_SERVERS];
static size_t ntp_server_count = 0;

//utils
#define IS_ON_XMB		(GetCooperationMode() == 0)
#define BEEP1			{ system_call_3(SC_RING_BUZZER, 0x1004, 0x4, 0x6); }
#define NOTIFY(msg)  do { if (ntp_config.notify_enable) vshtask_notify(msg); } while (0)

static void log_write(const char *fmt, ...);
#define LOG_WRITE(...) do { if (ntp_config.log_enable) log_write(__VA_ARGS__); } while (0)

static volatile bool running = false;
sys_ppu_thread_t worker_thread_id = (uint64_t)NONE;

typedef struct
{
	uint8_t li_vn_mode;
	uint8_t stratum;
	int8_t poll;
	int8_t precision;

	uint32_t root_delay;
	uint32_t root_dispersion;
	uint32_t ref_id;

	uint32_t ref_timestamp_sec;
	uint32_t ref_timestamp_frac;

	uint32_t orig_timestamp_sec;
	uint32_t orig_timestamp_frac;

	uint32_t recv_timestamp_sec;
	uint32_t recv_timestamp_frac;
	
	uint32_t trans_timestamp_sec;
	uint32_t trans_timestamp_frac;
	
} ntp_packet_t;

typedef struct
{
	int64_t offset_ns;
	int64_t delay_ns;
	uint8_t stratum;
} ntp_sample_t;

typedef enum
{
	CFG_CREATE_SUCCESS = 1,
	CFG_SUCCESS = 0,
	CFG_UNREADABLE = -1,
	CFG_CREATE_FAIL = -2,
	CFG_WRITE_FAIL = -3,
	CFG_READ_FAIL = -4,
	CFG_INVALID = -5
} ntp_config_response_t;

typedef enum
{
	NTP_SUCCESS = 0,
	NTP_POLL_ERROR = -1,
	NTP_INCORRECT_MODE = -2,
	NTP_CLOCK_UNSYNC = -3,
	NTP_KOD = -4,
	NTP_INVALID = -5
} ntp_response_t;

typedef enum
{
	SL_CREATE_SUCCESS = 1,
	SL_SUCCESS = 0,
	SL_WRITE_FAIL = -1
} serverlist_response_t;

static void wait_for_xmb(int timeout_s)
{
	uint8_t t = 0;
	while (running && (View_Find("explore_plugin") == 0))
	{
		if (++t > timeout_s)
		{
			break;
		}
		sys_timer_sleep(1);
	}
}

static inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(SC_PPU_THREAD_EXIT, val);
}

static inline sys_prx_id_t prx_get_module_id_by_address(void *addr)
{
	system_call_1(SC_GET_PRX_MODULE_BY_ADDRESS, (uint64_t)(uint32_t)addr);
	return (sys_prx_id_t)p1;
}

static void sys_prx_stop_prx_module(sys_prx_id_t prx, uint64_t meminfo)
{
	system_call_3(SC_STOP_PRX_MODULE, prx, 0, meminfo);
}

static inline uint32_t sys_get_current_time(uint64_t *sec, uint64_t *nsec)
{
	system_call_2(SC_GET_CURRENT_TIME, (uint64_t)sec, (uint64_t)nsec);
	return (uint32_t)p1;
}

static inline uint32_t sys_set_current_time(uint64_t sec, uint64_t nsec)
{
	system_call_2(SC_SET_CURRENT_TIME, (uint64_t)sec, (uint64_t)nsec);
	return (uint32_t)p1;
}

static void ns_to_ntp(int64_t ns, uint32_t *sec_be, uint32_t *frac_be)
{
	uint64_t unix_s = (uint64_t)(ns / 1000000000LL);
	uint64_t sub_ns = (uint64_t)(ns % 1000000000LL);
	uint64_t frac = (sub_ns << 32) / 1000000000ULL;

	*sec_be = htonl((uint32_t)(unix_s + NTP_TIMESTAMP_DELTA));
	*frac_be = htonl((uint32_t)frac);
}

static int64_t ntp_to_ns(uint32_t sec, uint32_t frac)
{
	int64_t s = (int64_t)ntohl(sec) - (int64_t)NTP_TIMESTAMP_DELTA;
	uint64_t f = ntohl(frac);
	return s * 1000000000LL + (int64_t)((f * 1000000000ULL) >> 32);
}

static int64_t now_ns(void)
{
	uint64_t sec = 0, nsec = 0;
	sys_get_current_time(&sec, &nsec);
	return (int64_t)sec * 1000000000LL + (int64_t)nsec;
}


static void set_system_time_ns(int64_t target)
{
	if (target < 0) return;

	uint64_t sec = (uint64_t)(target / 1000000000LL);
	uint64_t nsec = (uint64_t)(target % 1000000000LL);

	sys_set_current_time(sec, nsec);
}

static void log_write(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf)-1, fmt, ap);
	va_end(ap);

	if (n < 0)
	{
		return;
	}

	size_t len = ((size_t)n < sizeof(buf)-1) ? (size_t)n : sizeof(buf)-2;
	buf[len++] = '\n';
	buf[len] = '\0';

	int fd;

	if (cellFsOpen(LOG_FILE, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_APPEND, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		uint64_t written;
		cellFsWrite(fd, buf, len, &written);
		cellFsClose(fd);
	}

}


static ntp_config_response_t load_config(void)
{
	int fd, ret, lineno, bad, i;
	uint64_t nread, nwritten;
	static char buf[4096];
	static char line[256];
	char *p, *hash, *key, *eq, *val, *end, *endp;
	unsigned long long u;
	long long s;
	size_t len, used;

	ntp_config = ntp_config_default;

	ret = cellFsOpen(CFG_FILE, CELL_FS_O_RDONLY, &fd, NULL, 0);
	if (ret != CELL_FS_OK)
	{
		if (ret != CELL_FS_ENOENT)
		{
			LOG_WRITE("config file is unreadable, using defaults");
			return (ntp_config_response_t)CFG_UNREADABLE;
		}

		LOG_WRITE("config file doesn't exist, creating it");
		len = 0;

		len += snprintf(buf + len, sizeof(buf)-len, "ntp_poll_interval_s=%u\n", (unsigned)ntp_config.ntp_poll_interval_s);
		len += snprintf(buf + len, sizeof(buf)-len, "ntp_socket_timeout_s=%lld\n", (long long)ntp_config.ntp_socket_timeout_s);
		len += snprintf(buf + len, sizeof(buf)-len, "no_answer_interval_s=%u\n", (unsigned)ntp_config.no_answer_interval_s);
		len += snprintf(buf + len, sizeof(buf)-len, "drift_tolerance_ns=%lld\n", (long long)ntp_config.drift_tolerance_ns);
		len += snprintf(buf + len, sizeof(buf)-len, "log_enable=%d\n", ntp_config.log_enable ? 1 : 0);
		len += snprintf(buf + len, sizeof(buf)-len, "log_persist=%d\n", ntp_config.log_persist ? 1 : 0);
		len += snprintf(buf + len, sizeof(buf)-len, "notify_enable=%d\n", ntp_config.notify_enable ? 1 : 0);

		ret = cellFsOpen(CFG_FILE, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, &fd, NULL, 0);
		if (ret != CELL_FS_OK)
		{
			LOG_WRITE("failed to create config file");
			return (ntp_config_response_t)CFG_CREATE_FAIL;
		}

		ret = cellFsWrite(fd, buf, len, &nwritten);
		if (ret != CELL_FS_OK || nwritten != len)
		{
			LOG_WRITE("failed to write config file");
			cellFsClose(fd);
			cellFsUnlink(CFG_FILE);
			return (ntp_config_response_t)CFG_WRITE_FAIL;
		}

		if (cellFsClose(fd) != CELL_FS_OK)
		{
			LOG_WRITE("failed to close config file");
			cellFsUnlink(CFG_FILE);
			return (ntp_config_response_t)CFG_WRITE_FAIL;
		}

		return (ntp_config_response_t)CFG_CREATE_SUCCESS;
	}

	ret = cellFsRead(fd, buf, sizeof(buf)-1, &nread);
	cellFsClose(fd);
	if (ret != CELL_FS_OK)
	{
		LOG_WRITE("failed to read existing config file");
		return (ntp_config_response_t)CFG_READ_FAIL;
	}

	used = (size_t)nread;
	buf[used] = '\0';

	lineno = 0;
	p = buf;
	while (*p != '\0')
	{
		i = 0;
		while (*p != '\0' && *p != '\n' && i < (int)sizeof(line)-1)
		{
			line[i++] = *p++;
		}
		line[i] = '\0';
		while (*p != '\0' && *p != '\n')
		{
			p++;
		}
		if (*p == '\n')
		{
			p++;
		}

		lineno++;
		bad = 0;

		//# comments
		hash = strchr(line, '#');
		if (hash != NULL)
		{
			*hash = '\0';
		}

		//comments
		hash = strstr(line, "//");
		if (hash != NULL)
		{
			*hash = '\0';
		}

		//l whitespace
		key = line;
		while (*key == ' ' || *key == '\t' || *key == '\r')
		{
			key++;
		}

		if (*key == '\0')
		{
			continue;
		}

		eq = strchr(key, '=');
		if (eq == NULL)
		{
			bad = 1;
		}
		else
		{
			//t whitespace
			end = eq;
			while (end > key && (end[-1] == ' ' || end[-1] == '\t'))
			{
				end--;
			}
			*end = '\0';

			//whitespace around value
			val = eq + 1;
			while (*val == ' ' || *val == '\t')
			{
				val++;
			}

			end = val + strlen(val);
			while (end > val && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
			{
				end--;
			}
			*end = '\0';

			if (strcmp(key, "ntp_poll_interval_s") == 0)
			{
				if (val[0] == '-' || val[0] == '\0')
				{
					bad = 1;
				}
				else
				{
					u = strtoull(val, &endp, 10);
					if (*endp != '\0' || u > UINT32_MAX)
					{
						bad = 1;
					}
					else
					{
						ntp_config.ntp_poll_interval_s = (uint32_t)u;
					}
				}
			}
			else if (strcmp(key, "ntp_socket_timeout_s") == 0)
			{
				s = strtoll(val, &endp, 10);
				if (endp == val || *endp != '\0')
				{
					bad = 1;
				}
				else
				{
					ntp_config.ntp_socket_timeout_s = (int64_t)s;
				}
			}
			else if (strcmp(key, "no_answer_interval_s") == 0)
			{
				if (val[0] == '-' || val[0] == '\0')
				{
					bad = 1;
				}
				else
				{
					u = strtoull(val, &endp, 10);
					if (*endp != '\0' || u > UINT32_MAX)
					{
						bad = 1;
					}
					else
					{
						ntp_config.no_answer_interval_s = (uint32_t)u;
					}
				}
			}
			else if (strcmp(key, "drift_tolerance_ns") == 0)
			{
				s = strtoll(val, &endp, 10);
				if (endp == val || *endp != '\0')
				{
					bad = 1;
				}
				else
				{
					ntp_config.drift_tolerance_ns = (int64_t)s;
				}
			}
			else if (strcmp(key, "log_enable") == 0)
			{
				if (val[1] != '\0' || (val[0] != '0' && val[0] != '1'))
				{
					bad = 1;
				}
				else
				{
					ntp_config.log_enable = (val[0] == '1');
				}
			}
			else if (strcmp(key, "log_persist") == 0)
			{
				if (val[1] != '\0' || (val[0] != '0' && val[0] != '1'))
				{
					bad = 1;
				}
				else
				{
					ntp_config.log_persist = (val[0] == '1');
				}
			}
			else if (strcmp(key, "notify_enable") == 0)
			{
				if (val[1] != '\0' || (val[0] != '0' && val[0] != '1'))
				{
					bad = 1;
				}
				else
				{
					ntp_config.notify_enable = (val[0] == '1');
				}
			}
			else
			{
				bad = 1;
			}
		}

		if (bad)
		{
			LOG_WRITE("ntp config: bad line %d, using defaults", lineno);

			char msg[1024];
			snprintf(msg, sizeof(msg), "PS3NTP Configuration Error\nBad line #%d, using defaults.", lineno);
			NOTIFY(msg);
			ntp_config = ntp_config_default;
			return (ntp_config_response_t)CFG_INVALID;
		}
	}

	return (ntp_config_response_t)CFG_SUCCESS;
}


static serverlist_response_t load_serverlist(void)
{
	static char file_buf[SL_FILE_BUF_SIZE];
	size_t total = 0;

	int fd;

	if (cellFsOpen(SL_FILE, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		while (total < sizeof(file_buf)-1)
		{
			uint64_t n = 0;
			if (cellFsRead(fd, file_buf + total, sizeof(file_buf)-1 - total, &n) != CELL_FS_SUCCEEDED || n == 0)
			{
				break;
			}

			total += (size_t)n;
		}
		cellFsClose(fd);
	}
	file_buf[total] = '\0';

	char *p = file_buf;
	while (*p && ntp_server_count < NTP_MAX_SERVERS)
	{
		char *line = p;
		char *eol = strchr(p, '\n');

		if (eol)
		{
			*eol = '\0';
			p = eol + 1;
		}
		else
		{
			p += strlen(p);
		}

		while (*line == ' ' || *line == '\t') line++;
		char *end = line + strlen(line);

		while (end > line && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
		{
			*--end = '\0';
		}

		if (*line == '\0')
		{
			continue;
		}	

		if (strlen(line) >= NTP_SERVER_LEN)
		{
			continue;
		}

		strcpy(server_buf[ntp_server_count], line);
		ntp_servers[ntp_server_count] = server_buf[ntp_server_count];
		ntp_server_count++;
	}

	if (ntp_server_count == 0)
	{
		LOG_WRITE("serverlist empty or missing, recreating with defaults");
		ntp_servers[0] = NTP_DEFAULT_SERVER;
		ntp_server_count = 1;

		int nfd;

		if (cellFsOpen(SL_FILE, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, &nfd, NULL, 0) == CELL_FS_SUCCEEDED)
		{
			const char *default_line = NTP_DEFAULT_SERVER "\n";
			uint64_t written = 0;
			cellFsWrite(nfd, default_line, strlen(default_line), &written);
			cellFsClose(nfd);
			return (serverlist_response_t)SL_CREATE_SUCCESS;
		}
		else
		{
			LOG_WRITE("failed to create " SL_FILE);
			return (serverlist_response_t)SL_WRITE_FAIL;
		}
	}
	LOG_WRITE("loaded %d ntp servers", ntp_server_count);
	return (serverlist_response_t)SL_SUCCESS;
}


static void sync_clock(const ntp_sample_t *s)
{
	int64_t drift = s->offset_ns;
	if (drift < 0)
	{
		drift = -drift;
	}

	if (drift < ntp_config.drift_tolerance_ns) return; //close enuff

	LOG_WRITE("drift: %lld > tolerance: %lld... setting system clock", (long long)drift, (long long)ntp_config.drift_tolerance_ns);

	set_system_time_ns(now_ns() + s->offset_ns);
}

static ntp_response_t poll_ntp(const char *ntp_server, ntp_sample_t *out)
{
	ntp_packet_t req;
	ntp_packet_t resp;

	struct sockaddr_in server;
	
	struct timeval timeout = { ntp_config.ntp_socket_timeout_s, 0 };

	struct hostent *host = gethostbyname(ntp_server);
	if (host == NULL || host->h_addr_list[0] == NULL)
	{
		return (ntp_response_t)NTP_POLL_ERROR;
	}

	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(123);
	memcpy(&server.sin_addr, host->h_addr_list[0], host->h_length);

	int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0)
	{
		return (ntp_response_t)NTP_POLL_ERROR;
	}

	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	memset(&req, 0, sizeof(req));
	req.li_vn_mode = 0x23; 

	int64_t t1 = now_ns();
	ns_to_ntp(t1, &req.trans_timestamp_sec, &req.trans_timestamp_frac);

	if (sendto(sockfd, &req, sizeof(req), 0, (struct sockaddr *)&server, sizeof(server)) != (ssize_t)sizeof(req))
	{
		close(sockfd);
		return (ntp_response_t)NTP_POLL_ERROR;
	}

	ssize_t recieved = recv(sockfd, &resp, sizeof(resp), 0);
	int64_t t4 = now_ns();

	close(sockfd);

	if (recieved != (ssize_t)sizeof(resp))
	{
		return (ntp_response_t)NTP_POLL_ERROR;
	}

	if ((resp.li_vn_mode & 0x07) != 4) return (ntp_response_t)NTP_INCORRECT_MODE;
	if ((resp.li_vn_mode & 0xC0) == 0xC0) return (ntp_response_t)NTP_CLOCK_UNSYNC;
	if (resp.stratum == 0 || resp.stratum > 15) return (ntp_response_t)NTP_KOD;
	if (resp.orig_timestamp_sec != req.trans_timestamp_sec || resp.orig_timestamp_frac != req.trans_timestamp_frac) return (ntp_response_t)NTP_INVALID;

	int64_t t2 = ntp_to_ns(resp.recv_timestamp_sec, resp.recv_timestamp_frac);
	int64_t t3 = ntp_to_ns(resp.trans_timestamp_sec, resp.trans_timestamp_frac);

	out->delay_ns = (t4 - t1) - (t3 - t2);
	out->offset_ns = ((t2 - t1) + (t3 - t4)) / 2;
	out->stratum = resp.stratum;

	if (out->delay_ns < 0) return (ntp_response_t)NTP_INVALID;

	return 	(ntp_response_t)NTP_SUCCESS;

}

static void worker_thread(uint64_t arg)
{
	(void)arg;
	//do our file ops first
	cellFsMkdir(BASE_PATH, CELL_FS_S_IFDIR | 0777);

	if (load_config() < (ntp_config_response_t)CFG_SUCCESS)
	{
		NOTIFY("PS3NTP Using default configuration");
	}

	if (ntp_config.log_persist)
	{
		LOG_WRITE("ps3ntp worker started (persistent log mode)");
	}
	else
	{
		cellFsUnlink(LOG_FILE);
		LOG_WRITE("ps3ntp worker started");
	}

	if (load_serverlist() < (serverlist_response_t)SL_SUCCESS)
	{
		NOTIFY("Failed to load serverlist, using default NTP server.");
	}

	wait_for_xmb(30);
	NOTIFY(PS3NTPTITLE PS3NTPVERSION "\r\n" PS3NTPAUTHOR);
	bool init = true;   //initial sync when loaded

	while (running)
	{
		uint32_t wait_s = ntp_config.ntp_poll_interval_s;

		if (init || IS_ON_XMB)
		{
			const char *synced_with = NULL;

			for (size_t i = 0; i < ntp_server_count && running; i++)
			{
				ntp_sample_t sample;
				ntp_response_t ret = poll_ntp(ntp_servers[i], &sample);

				if (ret == (ntp_response_t)NTP_SUCCESS)
				{
					sync_clock(&sample);
					synced_with = ntp_servers[i];
					LOG_WRITE("offset_ns: %lld, delay_ns: %lld, stratum: %d, server: %s", (long long)sample.offset_ns, (long long)sample.delay_ns, (int)sample.stratum, ntp_servers[i]);
					break;
				}
				else
				{
					LOG_WRITE("ntp server: %s, error: %d", ntp_servers[i], (int)ret);
				}
			}

			if (synced_with)
			{
				char msg[1024];
				snprintf(msg, sizeof(msg), "Clock synced with %s!", synced_with);
				NOTIFY(msg);

				init = false;                  //only clear on success
				wait_s = ntp_config.ntp_poll_interval_s;     //next sync after the normal interval
				LOG_WRITE("current interval: %d (normal)", wait_s);
			}
			else
			{
				wait_s = ntp_config.no_answer_interval_s;    //retry sooner, initial stays true
				LOG_WRITE("current interval: %d (no answer)", wait_s);
				char msg[1024];
				snprintf(msg, sizeof(msg), "NTP server(s) are unreachable. Retrying in %d seconds.", wait_s);
				LOG_WRITE("all servers unreachable... not synced!");
				NOTIFY(msg);
			}
		}
		else
		{
			wait_s = XMB_CHECK_INTERVAL_S;
			LOG_WRITE("current interval: %d (waiting for xmb)", XMB_CHECK_INTERVAL_S);
		}

		for (uint32_t s = 0; s < wait_s && running; s++)
		{
			sys_timer_sleep(1);
		}
	}
	sys_ppu_thread_exit(0);
}

static void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = prx_get_module_id_by_address((void*)finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	sys_prx_stop_prx_module(prx, (uint64_t)(uint32_t)meminfo);
}

static void exit_thread(uint64_t arg)
{
	(void)arg;

	running = false;
	uint64_t ret = 0;

	if (worker_thread_id != (uint64_t)NONE)
	{
		sys_ppu_thread_join(worker_thread_id, &ret);
		worker_thread_id = (uint64_t)NONE;
	}
	sys_ppu_thread_exit(0);
}
extern "C" int ps3ntp_stop(void)
{
	uint64_t ret = 0;
	sys_ppu_thread_t exit_thread_id;

	sys_ppu_thread_create(&exit_thread_id, exit_thread, NULL, 0x000, (32 * 1024), SYS_PPU_THREAD_CREATE_JOINABLE, "ps3load_exit");
	sys_ppu_thread_join(exit_thread_id, &ret);

	finalize_module();

	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

extern "C" int ps3ntp_entry(void)
{
	if (running)
	{
		return SYS_PRX_RESIDENT;
	}

	running = true;

	sys_ppu_thread_create(&worker_thread_id, worker_thread, 0, 1000, (64 * 1024), SYS_PPU_THREAD_CREATE_JOINABLE, "ps3ntp");
	return SYS_PRX_RESIDENT;
}
