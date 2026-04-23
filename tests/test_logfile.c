#include "test_common.h"

#include "attrib.h"
#include "logfile.h"
#include "mst.h"

static u8 *g_logfile_data;
static size_t g_logfile_size;

s64 ntfs_attr_pread(ntfs_attr *na __attribute__((unused)), const s64 pos,
		s64 count, void *b)
{
	if (pos < 0 || count < 0 || (u64)pos > g_logfile_size)
		return -1;
	if ((u64)(pos + count) > g_logfile_size)
		count = (s64)(g_logfile_size - (size_t)pos);
	memcpy(b, g_logfile_data + pos, (size_t)count);
	return count;
}

static u32 sequence_bits(u64 file_size)
{
	u32 bits = 0;

	while (file_size) {
		file_size >>= 1;
		bits++;
	}
	return 67U - bits;
}

static void fill_restart_page(u8 *page, u32 system_page_size, u64 file_size,
		BOOL clean)
{
	RESTART_PAGE_HEADER *rp = (RESTART_PAGE_HEADER*)(void*)page;
	RESTART_AREA *ra;
	u16 *usa;
	int i;

	memset(page, 0, system_page_size);
	rp->magic = magic_RSTR;
	rp->usa_ofs = cpu_to_le16((u16)sizeof(*rp));
	rp->usa_count = cpu_to_le16(1 + (system_page_size >> NTFS_BLOCK_SIZE_BITS));
	rp->system_page_size = cpu_to_le32(system_page_size);
	rp->log_page_size = cpu_to_le32(DefaultLogPageSize);
	rp->restart_area_offset = cpu_to_le16(56);
	rp->major_ver = cpu_to_sle16(1);
	rp->minor_ver = cpu_to_sle16(1);
	rp->usn = cpu_to_le16(0xa55a);

	usa = (u16*)(void*)(page + le16_to_cpu(rp->usa_ofs));
	usa[0] = rp->usn;
	for (i = 1; i < le16_to_cpu(rp->usa_count); i++) {
		usa[i] = 0;
		*(u16*)(void*)(page + i * NTFS_BLOCK_SIZE - sizeof(u16)) = rp->usn;
	}

	ra = (RESTART_AREA*)(void*)(page + le16_to_cpu(rp->restart_area_offset));
	ra->current_lsn = cpu_to_sle64(7);
	ra->log_clients = cpu_to_le16(1);
	ra->client_free_list = LOGFILE_NO_CLIENT;
	ra->client_in_use_list = clean ? LOGFILE_NO_CLIENT : cpu_to_le16(0);
	ra->flags = clean ? RESTART_VOLUME_IS_CLEAN : 0;
	ra->seq_number_bits = cpu_to_le32(sequence_bits(file_size));
	ra->restart_area_length = cpu_to_le16((u16)(48 + sizeof(LOG_CLIENT_RECORD)));
	ra->client_array_offset = cpu_to_le16(48);
	ra->file_size = cpu_to_sle64((s64)file_size);
	ra->log_record_header_length = cpu_to_le16(48);
	ra->log_page_data_offset = cpu_to_le16(64);
}

static void test_logfile_restart_page_parsing(void)
{
	ntfs_volume vol;
	ntfs_inode inode;
	ntfs_attr log_attr;
	RESTART_PAGE_HEADER *restart_page = NULL;
	const size_t logfile_size = DefaultLogPageSize * 50U;

	g_logfile_data = calloc(1u, logfile_size);
	TEST_ASSERT(g_logfile_data != NULL);
	g_logfile_size = logfile_size;
	fill_restart_page(g_logfile_data, DefaultLogPageSize, logfile_size, TRUE);

	memset(&vol, 0, sizeof(vol));
	memset(&inode, 0, sizeof(inode));
	memset(&log_attr, 0, sizeof(log_attr));
	inode.vol = &vol;
	log_attr.ni = &inode;
	log_attr.data_size = logfile_size;

	TEST_ASSERT(ntfs_check_logfile(&log_attr, &restart_page));
	TEST_ASSERT(restart_page != NULL);
	TEST_ASSERT(ntfs_is_logfile_clean(&log_attr, restart_page));

	free(restart_page);
	free(g_logfile_data);
	g_logfile_data = NULL;
	g_logfile_size = 0;
}

static void test_logfile_unclean_detection(void)
{
	u8 page[DefaultLogPageSize];
	ntfs_volume vol;
	ntfs_inode inode;
	ntfs_attr log_attr;

	fill_restart_page(page, DefaultLogPageSize, DefaultLogPageSize * 50U, FALSE);

	memset(&vol, 0, sizeof(vol));
	memset(&inode, 0, sizeof(inode));
	memset(&log_attr, 0, sizeof(log_attr));
	inode.vol = &vol;
	log_attr.ni = &inode;

	TEST_ASSERT(!ntfs_is_logfile_clean(&log_attr,
		(RESTART_PAGE_HEADER*)(void*)page));
}

static void test_logfile_corpus_case(const char *path __attribute__((unused)),
		const char *name, const unsigned char *data, size_t size,
		void *opaque __attribute__((unused)))
{
	ntfs_volume vol;
	ntfs_inode inode;
	ntfs_attr log_attr;
	RESTART_PAGE_HEADER *restart_page = NULL;

	memset(&vol, 0, sizeof(vol));
	memset(&inode, 0, sizeof(inode));
	memset(&log_attr, 0, sizeof(log_attr));
	inode.vol = &vol;
	log_attr.ni = &inode;
	log_attr.data_size = (s64)size;
	g_logfile_data = (u8*)(uintptr_t)data;
	g_logfile_size = size;

	if (test_name_has_prefix(name, "log_bad_")) {
		TEST_ASSERT(!ntfs_check_logfile(&log_attr, &restart_page));
		return;
	}
	TEST_ASSERT(ntfs_check_logfile(&log_attr, &restart_page));
	TEST_ASSERT(restart_page != NULL);
	if (test_name_has_prefix(name, "log_clean_")) {
		TEST_ASSERT(ntfs_is_logfile_clean(&log_attr, restart_page));
	} else {
		TEST_ASSERT(test_name_has_prefix(name, "log_dirty_"));
		TEST_ASSERT(!ntfs_is_logfile_clean(&log_attr, restart_page));
	}
	free(restart_page);
}

static void test_logfile_corpus(void)
{
	test_for_each_corpus_file("tests/corpus/logfile", 20,
		test_logfile_corpus_case, NULL);
}

int main(void)
{
	TEST_RUN(test_logfile_restart_page_parsing);
	TEST_RUN(test_logfile_unclean_detection);
	TEST_RUN(test_logfile_corpus);
	return 0;
}
