#include "test_common.h"

#include "security.h"
#include "acls.h"

struct sid_buffer {
	SID sid;
	le32 extra[SID_MAX_SUB_AUTHORITIES - 1];
};

static SID *make_sid(struct sid_buffer *buffer, u64 authority,
		const u32 *sub_authorities, u8 count)
{
	u8 *value = buffer->sid.identifier_authority.value;
	u8 i;

	memset(buffer, 0, sizeof(*buffer));
	buffer->sid.revision = SID_REVISION;
	buffer->sid.sub_authority_count = count;
	for (i = 0; i < 6; i++)
		value[i] = (u8)(authority >> ((5 - i) * 8));
	for (i = 0; i < count; i++)
		buffer->sid.sub_authority[i] = cpu_to_le32(sub_authorities[i]);
	return &buffer->sid;
}

static size_t sid_bytes(const SID *sid)
{
	return (size_t)ntfs_sid_size(sid);
}

static u32 rol32(u32 value, unsigned int shift)
{
	return (value << shift) | (value >> (32U - shift));
}

static le32 manual_security_hash(const SECURITY_DESCRIPTOR_RELATIVE *sd, u32 len)
{
	const le32 *pos = (const le32*)sd;
	const le32 *end = pos + (len >> 2);
	u32 hash = 0;

	while (pos < end) {
		hash = le32_to_cpup(pos) + rol32(hash, 3);
		pos++;
	}
	return cpu_to_le32(hash);
}

static unsigned int build_security_descriptor(u8 *buffer, const SID *owner,
		const SID *group, const SID *ace_sid)
{
	SECURITY_DESCRIPTOR_RELATIVE *sd =
		(SECURITY_DESCRIPTOR_RELATIVE*)(void*)buffer;
	ACL *dacl;
	ACCESS_ALLOWED_ACE *ace;
	unsigned int offset = sizeof(*sd);
	unsigned int ace_size;
	unsigned int dacl_size;

	memset(buffer, 0, 256);
	sd->revision = SECURITY_DESCRIPTOR_REVISION;
	sd->control = cpu_to_le16(SE_SELF_RELATIVE | SE_DACL_PRESENT);

	sd->owner = cpu_to_le32(offset);
	memcpy(buffer + offset, owner, sid_bytes(owner));
	offset += (unsigned int)sid_bytes(owner);

	sd->group = cpu_to_le32(offset);
	memcpy(buffer + offset, group, sid_bytes(group));
	offset += (unsigned int)sid_bytes(group);

	sd->dacl = cpu_to_le32(offset);
	dacl = (ACL*)(void*)(buffer + offset);
	dacl->revision = ACL_REVISION;
	dacl->ace_count = cpu_to_le16(1);

	ace = (ACCESS_ALLOWED_ACE*)(void*)(buffer + offset + sizeof(*dacl));
	ace->type = ACCESS_ALLOWED_ACE_TYPE;
	ace->mask = FILE_READ_DATA;
	memcpy(&ace->sid, ace_sid, sid_bytes(ace_sid));

	ace_size = 8U + (unsigned int)sid_bytes(ace_sid);
	ace->size = cpu_to_le16((u16)ace_size);
	dacl_size = (unsigned int)sizeof(*dacl) + ace_size;
	dacl->size = cpu_to_le16((u16)dacl_size);
	return offset + dacl_size;
}

static void test_sid_validation_and_formatting(void)
{
	struct sid_buffer admin_buffer;
	struct sid_buffer pattern_buffer;
	struct sid_buffer invalid_buffer;
	const u32 admin_subs[] = { 32, 544 };
	const u32 pattern_good[] = { 21, 1000 };
	const u32 pattern_bad_low[] = { 21, 999 };
	const u32 pattern_bad_high[] = { 21, 0x80000000U };
	SID *admin = make_sid(&admin_buffer, 5, admin_subs, 2);
	char sid_string[64];
	char *allocated_sid;

	TEST_ASSERT(ntfs_valid_sid(admin));
	TEST_ASSERT_EQ_INT(ntfs_sid_size(admin), 16);
	TEST_ASSERT(ntfs_same_sid(admin, admin));
	TEST_ASSERT(ntfs_sid_to_mbs_size(admin) >=
		(int)strlen("S-1-5-32-544") + 1);
	TEST_ASSERT(ntfs_sid_to_mbs(admin, sid_string, sizeof(sid_string)) != NULL);
	TEST_ASSERT(strcmp(sid_string, "S-1-5-32-544") == 0);

	allocated_sid = ntfs_sid_to_mbs(admin, NULL, 0);
	TEST_ASSERT(allocated_sid != NULL);
	TEST_ASSERT(strcmp(allocated_sid, "S-1-5-32-544") == 0);
	free(allocated_sid);

	TEST_ASSERT(ntfs_valid_pattern(
		make_sid(&pattern_buffer, 5, pattern_good, 2)));
	TEST_ASSERT(!ntfs_valid_pattern(
		make_sid(&pattern_buffer, 5, pattern_bad_low, 2)));
	TEST_ASSERT(!ntfs_valid_pattern(
		make_sid(&pattern_buffer, 5, pattern_bad_high, 2)));

	make_sid(&invalid_buffer, 5, admin_subs, 2)->revision = 0;
	TEST_ASSERT(!ntfs_valid_sid(&invalid_buffer.sid));
}

static void test_security_descriptor_validation_and_hashing(void)
{
	struct sid_buffer owner_buffer;
	struct sid_buffer group_buffer;
	struct sid_buffer world_buffer;
	const u32 owner_subs[] = { 32, 544 };
	const u32 group_subs[] = { 32, 545 };
	const u32 world_subs[] = { 0 };
	u8 descriptor[256];
	unsigned int descriptor_size;
	SECURITY_DESCRIPTOR_RELATIVE *sd =
		(SECURITY_DESCRIPTOR_RELATIVE*)(void*)descriptor;
	ACL *dacl;
	ACCESS_ALLOWED_ACE *ace;
	le32 hash1;
	le32 hash2;
	char guid_str[40];
	NTFS_GUID guid = {
		.data1 = cpu_to_le32(0x12345678U),
		.data2 = cpu_to_le16(0x9abc),
		.data3 = cpu_to_le16(0xdef0),
		.data4 = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 },
	};

	descriptor_size = build_security_descriptor(descriptor,
		make_sid(&owner_buffer, 5, owner_subs, 2),
		make_sid(&group_buffer, 5, group_subs, 2),
		make_sid(&world_buffer, 1, world_subs, 1));

	TEST_ASSERT_EQ_INT((int)ntfs_attr_size((const char*)descriptor),
		(int)descriptor_size);
	TEST_ASSERT(ntfs_valid_descr((const char*)descriptor, descriptor_size));

	hash1 = ntfs_security_hash(sd, descriptor_size);
	TEST_ASSERT_EQ_INT((int)hash1, (int)manual_security_hash(sd, descriptor_size));

	descriptor[descriptor_size - 1] ^= 0x55;
	hash2 = ntfs_security_hash(sd, descriptor_size);
	TEST_ASSERT(hash1 != hash2);
	descriptor[descriptor_size - 1] ^= 0x55;

	dacl = (ACL*)(void*)(descriptor + le32_to_cpu(sd->dacl));
	ace = (ACCESS_ALLOWED_ACE*)(void*)((u8*)dacl + sizeof(*dacl));
	ace->size = cpu_to_le16((u16)(le16_to_cpu(ace->size) - 4));
	TEST_ASSERT(!ntfs_valid_descr((const char*)descriptor, descriptor_size));
	ace->size = cpu_to_le16((u16)(8 + sid_bytes(&ace->sid)));

	sd->owner = cpu_to_le32(2);
	TEST_ASSERT(!ntfs_valid_descr((const char*)descriptor, descriptor_size));
	sd->owner = cpu_to_le32(sizeof(*sd));

	TEST_ASSERT(ntfs_guid_to_mbs(&guid, guid_str) != NULL);
	TEST_ASSERT(strcmp(guid_str,
		"12345678-9abc-def0-1122-334455667788") == 0);
}

static void test_security_corpus_case(const char *path __attribute__((unused)),
		const char *name, const unsigned char *data, size_t size,
		void *opaque __attribute__((unused)))
{
	const SECURITY_DESCRIPTOR_RELATIVE *sd =
		(const SECURITY_DESCRIPTOR_RELATIVE*)(const void*)data;
	int expected_valid = test_name_has_prefix(name, "ok_");

	TEST_ASSERT(size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE));
	TEST_ASSERT_EQ_INT(ntfs_valid_descr((const char*)data, (unsigned int)size),
		expected_valid);
	if (expected_valid) {
		TEST_ASSERT_EQ_INT((int)ntfs_attr_size((const char*)data), (int)size);
		TEST_ASSERT_EQ_INT((int)ntfs_security_hash(sd, (u32)size),
			(int)manual_security_hash(sd, (u32)size));
	}
}

static void test_security_corpus(void)
{
	test_for_each_corpus_file("tests/corpus/acls_security", 20,
		test_security_corpus_case, NULL);
}

int main(void)
{
	TEST_RUN(test_sid_validation_and_formatting);
	TEST_RUN(test_security_descriptor_validation_and_hashing);
	TEST_RUN(test_security_corpus);
	return 0;
}
