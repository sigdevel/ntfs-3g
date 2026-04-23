#include "test_common.h"

#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

#include "logging.h"
#include "misc.h"

static int custom_handler_calls;
static u32 custom_handler_last_level;

static int custom_handler(const char *function __attribute__((unused)),
		const char *file __attribute__((unused)),
		int line __attribute__((unused)),
		u32 level,
		void *data __attribute__((unused)),
		const char *format __attribute__((unused)),
		ntfs_va_list args __attribute__((unused)))
{
	custom_handler_calls++;
	custom_handler_last_level = level;
	return 123;
}

static int call_fprintf_handler(FILE *stream, u32 level, const char *format, ...)
{
	int ret;
	ntfs_va_list args;

	ntfs_va_start(args, format);
	ret = ntfs_log_handler_fprintf("test_fn", "/tmp/test_file.c", 77,
			level, stream, format, args);
	ntfs_va_end(args);
	return ret;
}

static int call_generic_handler(ntfs_log_handler *handler, void *data,
		u32 level, const char *format, ...)
{
	int ret;
	ntfs_va_list args;

	ntfs_va_start(args, format);
	ret = handler("test_fn", "/tmp/test_file.c", 77, level, data, format, args);
	ntfs_va_end(args);
	return ret;
}

static char *read_stream(FILE *stream)
{
	long size;
	char *buf;

	TEST_ASSERT(stream != NULL);
	TEST_ASSERT(fflush(stream) == 0);
	TEST_ASSERT(fseek(stream, 0, SEEK_END) == 0);
	size = ftell(stream);
	TEST_ASSERT(size >= 0);
	TEST_ASSERT(fseek(stream, 0, SEEK_SET) == 0);
	buf = calloc((size_t)size + 1u, 1u);
	TEST_ASSERT(buf != NULL);
	TEST_ASSERT(fread(buf, 1u, (size_t)size, stream) == (size_t)size);
	return buf;
}

static void test_misc_allocators(void)
{
	unsigned char *buf;

	buf = ntfs_calloc(16);
	TEST_ASSERT(buf != NULL);
	for (int i = 0; i < 16; i++)
		TEST_ASSERT_EQ_INT(buf[i], 0);

	memset(buf, 0x5a, 16);
	buf = ntfs_realloc(buf, 32);
	TEST_ASSERT(buf != NULL);
	for (int i = 0; i < 16; i++)
		TEST_ASSERT_EQ_INT(buf[i], 0x5a);

	ntfs_free(buf);

	buf = ntfs_malloc(8);
	TEST_ASSERT(buf != NULL);
	ntfs_free(buf);
}

static void test_logging_levels_and_flags(void)
{
	u32 old_levels;
	u32 old_flags;

	old_levels = ntfs_log_get_levels();
	old_flags = ntfs_log_get_flags();

	(void)ntfs_log_set_levels(NTFS_LOG_LEVEL_DEBUG);
	TEST_ASSERT(ntfs_log_get_levels() & NTFS_LOG_LEVEL_DEBUG);
	(void)ntfs_log_clear_levels(NTFS_LOG_LEVEL_DEBUG);
	TEST_ASSERT(!(ntfs_log_get_levels() & NTFS_LOG_LEVEL_DEBUG));

	(void)ntfs_log_set_flags(NTFS_LOG_FLAG_PREFIX | NTFS_LOG_FLAG_FUNCTION);
	TEST_ASSERT(ntfs_log_get_flags() & NTFS_LOG_FLAG_PREFIX);
	TEST_ASSERT(ntfs_log_get_flags() & NTFS_LOG_FLAG_FUNCTION);
	(void)ntfs_log_clear_flags(NTFS_LOG_FLAG_FUNCTION);
	TEST_ASSERT(!(ntfs_log_get_flags() & NTFS_LOG_FLAG_FUNCTION));

	(void)ntfs_log_set_levels(old_levels);
	(void)ntfs_log_clear_levels(~old_levels);
	(void)ntfs_log_set_flags(old_flags);
	(void)ntfs_log_clear_flags(~old_flags);
}

static void test_logging_option_parser(void)
{
	TEST_ASSERT(ntfs_log_parse_option("--log-debug"));
	TEST_ASSERT(ntfs_log_get_levels() & NTFS_LOG_LEVEL_DEBUG);
	TEST_ASSERT(ntfs_log_parse_option("--log-trace"));
	TEST_ASSERT(ntfs_log_get_levels() & NTFS_LOG_LEVEL_TRACE);
	TEST_ASSERT(ntfs_log_parse_option("--log-verbose"));
	TEST_ASSERT(ntfs_log_get_levels() & NTFS_LOG_LEVEL_VERBOSE);
	TEST_ASSERT(ntfs_log_parse_option("--log-quiet"));
	TEST_ASSERT(!(ntfs_log_get_levels() & NTFS_LOG_LEVEL_QUIET));
	TEST_ASSERT(!ntfs_log_parse_option("--log-nope"));
}

static void test_logging_redirect_and_errno(void)
{
	int ret;
	int saved_errno;
	u32 levels;

	levels = ntfs_log_get_levels();
	(void)ntfs_log_set_levels(NTFS_LOG_LEVEL_INFO);
	ntfs_log_set_handler(custom_handler);

	custom_handler_calls = 0;
	custom_handler_last_level = 0;
	saved_errno = EBUSY;
	errno = saved_errno;

	ret = ntfs_log_redirect("fn", "file.c", 9, NTFS_LOG_LEVEL_INFO, NULL,
			"hello %d", 7);
	TEST_ASSERT_EQ_INT(ret, 123);
	TEST_ASSERT_EQ_INT(custom_handler_calls, 1);
	TEST_ASSERT_EQ_INT((int)custom_handler_last_level, NTFS_LOG_LEVEL_INFO);
	TEST_ASSERT_EQ_INT(errno, saved_errno);

	(void)ntfs_log_clear_levels(NTFS_LOG_LEVEL_INFO);
	ret = ntfs_log_redirect("fn", "file.c", 9, NTFS_LOG_LEVEL_INFO, NULL,
			"hidden");
	TEST_ASSERT_EQ_INT(ret, 0);
	TEST_ASSERT_EQ_INT(custom_handler_calls, 1);

	ntfs_log_set_handler(NULL);
	(void)ntfs_log_set_levels(levels);
}

static void test_logging_fprintf_handler(void)
{
	FILE *stream;
	char *output;
	u32 saved_flags;

	stream = tmpfile();
	TEST_ASSERT(stream != NULL);

	saved_flags = ntfs_log_get_flags();
	(void)ntfs_log_set_flags(NTFS_LOG_FLAG_PREFIX |
			NTFS_LOG_FLAG_FILENAME |
			NTFS_LOG_FLAG_LINE |
			NTFS_LOG_FLAG_FUNCTION);

	errno = ENOSPC;
	TEST_ASSERT(call_fprintf_handler(stream, NTFS_LOG_LEVEL_PERROR,
			"message %d", 42) > 0);
	output = read_stream(stream);
	TEST_ASSERT(strstr(output, "ERROR: ") != NULL);
	TEST_ASSERT(strstr(output, "test_file.c") != NULL);
	TEST_ASSERT(strstr(output, "(77)") != NULL);
	TEST_ASSERT(strstr(output, "test_fn(): ") != NULL);
	TEST_ASSERT(strstr(output, "message 42") != NULL);
	TEST_ASSERT(strstr(output, strerror(ENOSPC)) != NULL);
	free(output);

	fclose(stream);
	(void)ntfs_log_set_flags(saved_flags);
}

static void test_logging_stream_handlers(void)
{
	FILE *stream;
	char *output;
	int saved_stdout;
	int saved_stderr;
	int devnull;

	stream = tmpfile();
	TEST_ASSERT(stream != NULL);
	TEST_ASSERT(call_generic_handler(ntfs_log_handler_null, NULL,
			NTFS_LOG_LEVEL_INFO, "ignored") == 0);
	TEST_ASSERT(call_generic_handler(ntfs_log_handler_fprintf, NULL,
			NTFS_LOG_LEVEL_INFO, "ignored") == 0);
	TEST_ASSERT(call_generic_handler(ntfs_log_handler_stdout, stream,
			NTFS_LOG_LEVEL_INFO, "stdout-path") > 0);
	TEST_ASSERT(call_generic_handler(ntfs_log_handler_stderr, stream,
			NTFS_LOG_LEVEL_ERROR, "stderr-path") > 0);
	output = read_stream(stream);
	TEST_ASSERT(strstr(output, "stdout-path") != NULL);
	TEST_ASSERT(strstr(output, "stderr-path") != NULL);
	free(output);
	fclose(stream);

	devnull = open("/dev/null", O_WRONLY);
	TEST_ASSERT(devnull >= 0);
	saved_stdout = dup(STDOUT_FILENO);
	saved_stderr = dup(STDERR_FILENO);
	TEST_ASSERT(saved_stdout >= 0);
	TEST_ASSERT(saved_stderr >= 0);
	TEST_ASSERT(dup2(devnull, STDOUT_FILENO) >= 0);
	TEST_ASSERT(dup2(devnull, STDERR_FILENO) >= 0);

	TEST_ASSERT(call_generic_handler(ntfs_log_handler_stdout, NULL,
			NTFS_LOG_LEVEL_INFO, "stdout-default") > 0);
	TEST_ASSERT(call_generic_handler(ntfs_log_handler_stderr, NULL,
			NTFS_LOG_LEVEL_ERROR, "stderr-default") > 0);
	TEST_ASSERT(call_generic_handler(ntfs_log_handler_outerr, NULL,
			NTFS_LOG_LEVEL_INFO, "outerr-info") > 0);
	TEST_ASSERT(call_generic_handler(ntfs_log_handler_outerr, NULL,
			NTFS_LOG_LEVEL_ERROR, "outerr-error") > 0);

	TEST_ASSERT(dup2(saved_stdout, STDOUT_FILENO) >= 0);
	TEST_ASSERT(dup2(saved_stderr, STDERR_FILENO) >= 0);
	close(saved_stdout);
	close(saved_stderr);
	close(devnull);
}

int main(void)
{
	TEST_RUN(test_misc_allocators);
	TEST_RUN(test_logging_levels_and_flags);
	TEST_RUN(test_logging_option_parser);
	TEST_RUN(test_logging_redirect_and_errno);
	TEST_RUN(test_logging_fprintf_handler);
	TEST_RUN(test_logging_stream_handlers);
	return 0;
}
