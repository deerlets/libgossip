#ifndef __LIB_UTILS_H
#define __LIB_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *get_ifaddr();
bool check_ipaddr(const char *ipaddr);

char *make_iso8601_time(const long long *time);

void bytes_to_hexstr(const uint8_t *bytes, int len, char *hexstr);
void hexstr_to_bytes(const char *hexstr, uint8_t *bytes, size_t size);

int gcd(int n1, int n2);

char *do_sha1(const void *buf, size_t len);
char *uuid_v4_gen();
int strip_parenthesis(char *buf);

int char_to_int(unsigned char ch);
bool is_hex_char(const char c);
//检测str是否符合base进制数据规范
bool is_base_str(const char *str,int base);
// 合并为N进制的字符
char combine_to_n_system_char(char ch1, char ch2, int base);

bool ascii_buf_to_bin(const unsigned char *buf, unsigned int buf_len,
                       char *ret, int ret_len);

void swap_hight_low_positon(char *array, int high_pos, int low_pos);
// 去除字符串开头的'0'，再将src拷贝到dest

#ifdef __cplusplus
}
#endif
#endif
