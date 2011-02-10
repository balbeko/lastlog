/*
 * Copyright (C) 2011 Jason A. Donenfeld <Jason@zx2c4.com>
 * Copyright (C) 2011 Adam Weiss <Adam@signal11.com>
 */

#include <stdio.h>
#include <inttypes.h>
#include <utmp.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void do_file(char *filename);
void detect_filetype(FILE *fp);
void binary_to_text(FILE *fp);
void text_to_binary(FILE *fp);
void version();
void usage();

#if __WORDSIZE == 64 && defined __WORDSIZE_COMPAT32
#define UTMP_TEXT_FORMAT "%hd\t%d\t%s\t%s\t%s\t%s\t%hd\t%hd\t%"PRId32"\t%"PRId32"\t%"PRId32"\t%s\n"
#else
#define UTMP_TEXT_FORMAT "%hd\t%d\t%s\t%s\t%s\t%s\t%hd\t%hd\t%ld\t%ld\t%ld\t%s\n"
#endif
#define TEXT_SIZE 422

#define VERSION "0.1"
char *program_name;
static int read_binary_mode = -1;

int main(int argc, char **argv)
{
	program_name = argv[0];

	static struct option long_options[] = {
		{"version",	no_argument,	0,	'v'},
		{"help",	no_argument,	0,	'h'},
		{"binary",	no_argument,	0,	'b'},
		{"text",	no_argument,	0,	't'}
	};
	int c;
	int option_index;
	while (1) {
		c  = getopt_long(argc, argv, "tbhv", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 'h':
				usage();
				break;
			case 'v':
				version();
				break;
			case 't':
			case 'b':
				if (read_binary_mode == -1)
					read_binary_mode = (c == 'b') ? 1 : 0;
				else {
					printf("%s: You may not specify both binary and text mode\n", program_name);
					exit(EXIT_FAILURE);
				}
				break;
		}
	}

	if (optind < argc) {
		while (optind < argc)
			do_file(argv[optind++]);
	}
	else
		do_file("-");

	return 0;
}

void do_file(char *filename)
{
	FILE *fp;
	if (filename[0] == '-' && filename[1] == '\0')
		fp = stdin;
	else {
		fp = fopen(filename, "r");
		if (fp == NULL) {
			printf("%s: %s: %s\n", program_name, filename, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if (read_binary_mode == -1)
		detect_filetype(fp);

	if (read_binary_mode == 1)
		binary_to_text(fp);
	else
		text_to_binary(fp);

	if (fp != stdin)
		fclose(fp);
}

void detect_filetype(FILE *fp)
{
	// we only read 1 character and then ungetc it because you can't rewind stdin past 1 character
	int character = fgetc(fp);
	// every record should start with something less than ascii zero
	read_binary_mode = character < '0';
	ungetc(character, fp);
}

void binary_to_text(FILE *fp)
{
	struct utmp entry;
	char ip[16];
	while (fread(&entry, sizeof(struct utmp), 1, fp)) {
		if (0) //TODO: determine how to figure out of it's an ipv6. entries 1 2 and 3 are zero perhaps?
			ip[0] = 'a'; //TODO: format ipv6 address
		else
			sprintf(ip, "%d.%d.%d.%d",
				(entry.ut_addr_v6[0] >> 0x0) & 0xFF,
				(entry.ut_addr_v6[0] >> 0x8) & 0xFF,
				(entry.ut_addr_v6[0] >> 0x10) & 0xFF,
				(entry.ut_addr_v6[0] >> 0x18) & 0xFF);

		printf(UTMP_TEXT_FORMAT, entry.ut_type, entry.ut_pid, entry.ut_line,
					 entry.ut_id, entry.ut_user, entry.ut_host,
					 entry.ut_exit.e_termination, entry.ut_exit.e_exit,
					 entry.ut_session, entry.ut_tv.tv_sec,
					 entry.ut_tv.tv_usec, ip);
	}
}

void text_to_binary(FILE *fp)
{
	char parser[] = UTMP_TEXT_FORMAT;
	char *parse_tokens[12];
	char *parse_token;
	int i;
	for (parse_token = strtok(parser, "\t"), i = 0; parse_token && i < 12; parse_token = strtok(0, "\t"), ++i)
		parse_tokens[i] = parse_token;
	
	char ip[16];
	struct utmp entry;
	void *entities[] = { &entry.ut_type, &entry.ut_pid, entry.ut_line,
			     entry.ut_id, entry.ut_user, entry.ut_host,
			     &entry.ut_exit.e_termination, &entry.ut_exit.e_exit,
			     &entry.ut_session, &entry.ut_tv.tv_sec,
			     &entry.ut_tv.tv_usec, ip };

	unsigned char ip_n[4];
	char line[TEXT_SIZE];
	char *input_token;
	char *scanner;
	while (fgets(line, TEXT_SIZE, fp)) {
		memset(&entry, 0, sizeof(struct utmp));
		input_token = line;
		for (i = 0; i < 12; ++i) {
			scanner = input_token;
			do {
				if (*scanner == '\t') {
					*scanner = '\0';
					break;
				}
			} while (*++scanner != '\0'); //TODO: unsafe if no null
			//TODO: check to see if token is %s, and then just copy the pointer instead of scanfing it - unsafe otherwise
			sscanf(input_token, parse_tokens[i], entities[i]);
			//TODO: don't go past end of string if there aren't 12 entries - unsafe otherwise
			input_token = scanner + 1;
		}
		sscanf(ip, "%hhu.%hhu.%hhu.%hhu", &ip_n[0], &ip_n[1], &ip_n[2], &ip_n[3]);
		entry.ut_addr_v6[0] = ((ip_n[3] << 24) |
				       (ip_n[2] << 16) |
				       (ip_n[1] << 8)  |
				       (ip_n[0]));
		//TODO: ipv6 addresses...

		fwrite(&entry, sizeof(struct utmp), 1, stdout);
	}
}

void version()
{
	printf("utmpr "VERSION"\n");
	exit(EXIT_SUCCESS);
}

void usage()
{
	printf("Usage %s: [OPTION]... [FILE]...\n", program_name);
	printf("Convert FILE(s), or standard input from/to utmp binary format/text\n");
	printf("and print to standard output\n\n");

	printf("  -t, --text       read text format and write utmp binary\n");
	printf("  -b, --binary     read utmp binary and write text\n");
	printf("      --help       display this help and exit\n");
	printf("      --version    output version and exit\n");

	printf("\nWith no FILE, or when FILE is -, read standard input.\n");
	printf("If neither --text nor --binary specified, auto-detect.\n\n");

	printf("Your system wtmp is located at: "WTMP_FILENAME"\n");
	printf("Your system utmp is located at: "UTMP_FILENAME"\n");
	exit(EXIT_SUCCESS);
}
