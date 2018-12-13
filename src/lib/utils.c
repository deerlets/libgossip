#include "utils.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <regex.h>
#ifdef __unix
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif
#include <openssl/sha.h>
#include <openssl/rand.h>

#if 0
bool check_wireless(const char *ifname, char *protocol)
{
	int fd;
	struct iwreq req;
	memset(&req, 0, sizeof(req));
	strncpy(req.ifr_name, ifname, IFNAMSIZ);

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return false;

	if (ioctl(fd, SIOCGIWNAME, &req) != -1) {
		if (protocol) strncpy(protocol, req.u.name, IFNAMSIZ);
		close(fd);
		return true;
	}

	close(fd);
	return false;
}
#endif

const char *get_ifaddr()
{
	char *retval = NULL;

#ifdef __unix
	struct ifaddrs *ifaddrs = NULL;
	if (getifaddrs(&ifaddrs) == -1)
		return NULL;

	for (struct ifaddrs *pos = ifaddrs; pos != NULL; pos = pos->ifa_next) {
		// only support ipv4
		if (pos->ifa_addr && pos->ifa_addr->sa_family == AF_INET &&
		    pos->ifa_flags & IFF_UP &&
		    pos->ifa_flags & IFF_MULTICAST &&
		    !(pos->ifa_flags & IFF_LOOPBACK)) {
			retval = inet_ntoa(((struct sockaddr_in *)
			                    (pos->ifa_addr))->sin_addr);
			break;
		}
	}

	freeifaddrs(ifaddrs);
#else
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	struct hostent *info = gethostbyname("");
	retval = inet_ntoa(*(struct in_addr *) (*info->h_addr_list));
	WSACleanup();
#endif

	return retval;
}

bool check_ipaddr(const char *ipaddr)
{
	int rc;
	regex_t reg;

	int nmatch = 1;
	regmatch_t pmatch[1];

	assert(regcomp(&reg, "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+",
	               REG_EXTENDED) == 0);
	rc = regexec(&reg, ipaddr, nmatch, pmatch, 0);
	regfree(&reg);

	if (rc == 0)
		return true;
	else
		return false;
}

char *make_iso8601_time(const long long *_time)
{
	static char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime((time_t*)(_time)));
	return buf;
}

void bytes_to_hexstr(const uint8_t *bytes, int len, char *hexstr)
{
	const char hextab[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	                       'A', 'B', 'C', 'D', 'E', 'F'};

	for (int i = 0; i < len; i++) {
		hexstr[i * 2] = hextab[bytes[i] / 16];
		hexstr[i * 2 + 1] = hextab[bytes[i] % 16];
	}

	hexstr[len * 2] = 0;
}

void hexstr_to_bytes(const char *hexstr, uint8_t *bytes, size_t size)
{
	char tmp[3] = {0};
	char *endptr;
	int len = strlen(hexstr) >> 1;

	assert(size >= len);

	for (int i = 0; i < len; i++) {
		tmp[0] = *(hexstr + (i << 1));
		tmp[1] = *(hexstr + (i << 1) + 1);
		bytes[i] = strtol(tmp, &endptr, 16);
	}
}

int gcd(int n1, int n2)
{
	if (n1 == n2) return n1;

	n1 = (n1 > 0) ? n1 : -n1;
	n2 = (n2 > 0) ? n2 : -n2;

	if (n1 > n2)
		return gcd(n1 - n2, n2);
	else
		return gcd(n2 - n1, n1);
}

char *do_sha1(const void *buf, size_t len)
{
	char *retval;

	unsigned char md[SHA_DIGEST_LENGTH];
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, buf, len);
	SHA1_Final(md, &ctx);

	retval = malloc(SHA_DIGEST_LENGTH * 2 + 1);
	bytes_to_hexstr(md, SHA_DIGEST_LENGTH, retval);

	return retval;
}

char *uuid_v4_gen()
{
	char *retval;

	union {
		struct {
			uint32_t time_low;
			uint16_t time_mid;
			uint16_t time_hi_and_version;
			uint8_t  clk_seq_hi_res;
			uint8_t  clk_seq_low;
			uint8_t  node[6];
		};
		uint8_t __rnd[16];
	} uuid;

	if (RAND_bytes(uuid.__rnd, sizeof(uuid)) != 1)
		return NULL;

	// Refer Section 4.2 of RFC-4122
	// https://tools.ietf.org/html/rfc4122#section-4.2
	uuid.clk_seq_hi_res = (uint8_t)((uuid.clk_seq_hi_res & 0x3F) | 0x80);
	uuid.time_hi_and_version =
		(uint16_t)((uuid.time_hi_and_version & 0x0FFF) | 0x4000);

	retval = malloc(37);
	snprintf(retval, 37, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	         uuid.time_low, uuid.time_mid, uuid.time_hi_and_version,
	         uuid.clk_seq_hi_res, uuid.clk_seq_low,
	         uuid.node[0], uuid.node[1], uuid.node[2],
	         uuid.node[3], uuid.node[4], uuid.node[5]);

	return retval;
}

int strip_parenthesis(char *buf)
{
	char *begin = strchr(buf, '(');
	char *end = strchr(buf, ')');

	if (!begin || !end || begin >= end || end != buf + strlen(buf) - 1)
		return -1;

	*begin = 0;
	return 0;
}

int char_to_int(unsigned char ch)
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	else if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	else return ch - 'A' + 10;
}

bool is_hex_char(const char c)
{
	return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
	        (c >= 'a' && c <= 'f')) ? true : false;
}

bool ascii_buf_to_bin(const unsigned char *buf, unsigned int buf_len,
                      char *ret, int ret_len)
{
	if ((char *) buf == ret)return false;
	if (buf == NULL || buf_len % 2 != 0 || buf_len == 0) {
		return false;
	}
	if (ret == NULL) return false;
	if (ret_len != buf_len / 2) return false;
	int cout = 0;
	for (unsigned int i = 0; i < buf_len; i += 2) {
		int a = char_to_int(buf[i]);
		int b = char_to_int(buf[i + 1]);
		if (a == -1 || b == -1) {
			return false;
		} else {
			unsigned char c = a * 16 + b;
			ret[cout] = c;
			++cout;
		}
	}
	return true;
}

void swap_hight_low_positon(char *array, int high_pos, int low_pos)
{
	int i, j;
	char temp;
	if (high_pos > low_pos) {
		int tmp_i = high_pos;
		high_pos = low_pos;
		low_pos = tmp_i;
	}
	for (i = high_pos, j = low_pos; i < j; ++i, --j) {
		temp = array[i];
		array[i] = array[j];
		array[j] = temp;
	}
}

char combine_to_n_system_char(char ch1, char ch2, int base)
{
	return (char) (char_to_int(ch1) * base + char_to_int(ch2));
}

bool is_base_str(const char *str, int base)
{
// 判断是否符合base进制数的规范
// 支持16进制、10进制、8进制、2进制
	int str_len = strlen(str);
	switch (base) {
		case 16:
			for (int i = 0; i < str_len; ++i) {
				if (!is_hex_char(str[i]))
					return false;
			}
			break;
		case 10:
			for (int i = 0; i < str_len; ++i) {
				if (!(str[i] >= '0' && str[i] <= '9'))
					return false;
			}
			break;
		case 8:
			for (int i = 0; i < str_len; ++i) {
				if (!(str[i] >= '0' && str[i] <= '7'))
					return false;
			}
			break;
		case 2:
			for (int i = 0; i < str_len; ++i) {
				if (str[i] != '0' && str[i] != '1')
					return false;
			}
			break;
		default:
			return false;
	}
	return true;
}
