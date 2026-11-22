
#ifndef __STDC_H__
#define __STDC_H__

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

	int stdc_3A840AE3(char *str, size_t size, const char *fmt, ...);						//snprintf()
#define snprintf stdc_3A840AE3	

	int stdc_273B9711(char *str, const char *fmt, ...);										//sprintf()
#define sprintf stdc_273B9711

	int stdc_153B364A(const char *path, mode_t mode);										//mkdir()
#define mkdir stdc_153B364A

	int stdc_C3E14CBE(const void *ptr1, const void *ptr2, size_t num);						//memcmp()
#define __memcmp stdc_C3E14CBE

	int stdc_F356418C(const char *, int, ...);
#define open stdc_F356418C

	int stdc_B6D92AC3(const char *s1, const char *s2);										// strcasecmp()
#define strcasecmp stdc_B6D92AC3

	extern char *stdc_AA9635D7(char *dest, const char *src);								// strcat()
#define strcat stdc_AA9635D7

	extern ssize_t stdc_FA00D211(int fd, void *buf, size_t nbytes);                        // read()
#define read stdc_FA00D211

	extern ssize_t stdc_526A496A(int fildes, const void *buf, size_t nbytes);              // write()
#define write stdc_526A496A

	extern int stdc_B120F6CA(int fd);                                                     // close()
#define close stdc_B120F6CA

	extern int stdc_3D85D6F8(const char *str1, const char *str2);							// strcmp()
#define strcmp stdc_3D85D6F8

	extern int stdc_99A72146(char *s, size_t n, const char *fmt, va_list arg);            // vsnprintf()
#define vsnprintf stdc_99A72146

	extern char *stdc_DEBEE2AF(char *str, int c);                                         // strchr()
#define strchr stdc_DEBEE2AF

	uint64_t stdc_36C067C1(const char *a, char **b, int x);								// strtoll()
#define strtoll stdc_36C067C1

	unsigned long stdc_867275D7(const char *a, char **b, int x);						 // strtoull()
#define strtoull stdc_867275D7

	extern char *stdc_C5C09834(const char *str1, const char *str2);						// strstr()
#define strstr stdc_C5C09834

	static inline int memcmp(const void *ptr1, const void *ptr2, size_t num)
	{
		if (!ptr1 || !ptr2)
			return -1;

		if (!num)
			return 0;

		return __memcmp(ptr1, ptr2, num);
	}

	static inline void *memset(void *dest, int value, size_t count)
	{
		unsigned char *ptr = (unsigned char *)dest;

		while (count--)
			*ptr++ = (unsigned char)value;

		return dest;
	}

	static inline void *memcpy(void *dst0, const void *src0, size_t len0)
	{
		char *dst = (char *)dst0;
		const char *src = (const char *)src0;
		void *save = dst0;

		while (len0--)
			*dst++ = *src++;

		return save;
	}

	static inline size_t strlen(const char *str)
	{
		const char *start = str;

		while (*str)
			str++;

		return (size_t)(str - start);
	}

	static inline char *strcpy(char *dest, const char* src)
	{
		char *ret = dest;

		while ((*dest++ = *src++));

		return ret;
	}

	static inline int recv_all(int sock, void *buffer, size_t length)
	{
		uint8_t *ptr = (uint8_t*)buffer;
		size_t received = 0;

		while (received < length)
		{
			int ret = read(sock, ptr + received, length - received);

			if (ret <= 0)
				return ret;

			received += ret;
		}

		return received;
	}

#ifdef __cplusplus
}
#endif

#endif
