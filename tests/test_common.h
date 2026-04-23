#ifndef NTFS3G_TEST_COMMON_H
#define NTFS3G_TEST_COMMON_H

#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_ASSERT(expr)                                                       \
	do {                                                                    \
		if (!(expr)) {                                                  \
			fprintf(stderr, "Assertion failed: %s (%s:%d)\n",       \
				#expr, __FILE__, __LINE__);                      \
			abort();                                                \
		}                                                               \
	} while (0)

#define TEST_ASSERT_EQ_INT(actual, expected)                                    \
	do {                                                                    \
		int test_actual__ = (actual);                                   \
		int test_expected__ = (expected);                               \
		if (test_actual__ != test_expected__) {                         \
			fprintf(stderr,                                          \
				"Assertion failed: %s == %s (got %d, expected %d) at %s:%d\n", \
				#actual, #expected, test_actual__,                 \
				test_expected__, __FILE__, __LINE__);              \
			abort();                                                \
		}                                                               \
	} while (0)

#define TEST_ASSERT_EQ_LL(actual, expected)                                     \
	do {                                                                    \
		long long test_actual__ = (long long)(actual);                  \
		long long test_expected__ = (long long)(expected);              \
		if (test_actual__ != test_expected__) {                         \
			fprintf(stderr,                                          \
				"Assertion failed: %s == %s (got %lld, expected %lld) at %s:%d\n", \
				#actual, #expected, test_actual__,                 \
				test_expected__, __FILE__, __LINE__);              \
			abort();                                                \
		}                                                               \
	} while (0)

#define TEST_ASSERT_MEMEQ(actual, expected, size)                               \
	do {                                                                    \
		if (memcmp((actual), (expected), (size)) != 0) {                \
			fprintf(stderr, "Assertion failed: memory differs at %s:%d\n", \
				__FILE__, __LINE__);                              \
			abort();                                                \
		}                                                               \
	} while (0)

#define TEST_RUN(fn)                                                            \
	do {                                                                    \
		fn();                                                           \
		printf("%s: OK\n", #fn);                                       \
	} while (0)

typedef void (*test_corpus_callback)(const char *path, const char *name,
		const unsigned char *data, size_t size, void *opaque);

static __attribute__((unused)) unsigned char *test_read_file_bytes(
		const char *path, size_t *size_out)
{
	FILE *file;
	long length;
	unsigned char *data;

	file = fopen(path, "rb");
	TEST_ASSERT(file != NULL);
	TEST_ASSERT_EQ_INT(fseek(file, 0, SEEK_END), 0);
	length = ftell(file);
	TEST_ASSERT(length >= 0);
	TEST_ASSERT_EQ_INT(fseek(file, 0, SEEK_SET), 0);
	data = (unsigned char*)malloc((size_t)length);
	TEST_ASSERT(data != NULL || length == 0);
	if (length > 0) {
		TEST_ASSERT(fread(data, 1, (size_t)length, file) == (size_t)length);
	}
	fclose(file);
	*size_out = (size_t)length;
	return data;
}

static __attribute__((unused)) int test_name_has_prefix(const char *name,
		const char *prefix)
{
	return strncmp(name, prefix, strlen(prefix)) == 0;
}

static __attribute__((unused)) void test_for_each_corpus_file(
		const char *directory, size_t min_files,
		test_corpus_callback callback, void *opaque)
{
	DIR *dir;
	struct dirent *entry;
	size_t count = 0;

	dir = opendir(directory);
	TEST_ASSERT(dir != NULL);
	while ((entry = readdir(dir)) != NULL) {
		char *path;
		size_t path_len;
		size_t data_size;
		unsigned char *data;
		struct stat st;

		if (entry->d_name[0] == '.')
			continue;
		path_len = strlen(directory) + strlen(entry->d_name) + 2;
		path = (char*)malloc(path_len);
		TEST_ASSERT(path != NULL);
		snprintf(path, path_len, "%s/%s", directory, entry->d_name);
		TEST_ASSERT_EQ_INT(stat(path, &st), 0);
		if (!S_ISREG(st.st_mode)) {
			free(path);
			continue;
		}
		data = test_read_file_bytes(path, &data_size);
		callback(path, entry->d_name, data, data_size, opaque);
		free(data);
		free(path);
		count++;
	}
	closedir(dir);
	TEST_ASSERT(count >= min_files);
}

#endif
