#include "test_common.h"

#include "cache.h"

struct test_cache_entry {
	struct CACHED_GENERIC base;
	int key;
};

struct test_cache {
	struct CACHE_HEADER *cache;
	int entry_count;
	int max_hash;
};

static int freed_entries;

static int compare_entry(const struct CACHED_GENERIC *cached,
		const struct CACHED_GENERIC *item)
{
	const struct test_cache_entry *lhs =
		(const struct test_cache_entry*)(const void*)cached;
	const struct test_cache_entry *rhs =
		(const struct test_cache_entry*)(const void*)item;

	return lhs->key != rhs->key;
}

static int hash_entry(const struct CACHED_GENERIC *cached)
{
	const struct test_cache_entry *entry =
		(const struct test_cache_entry*)(const void*)cached;

	return entry->key & 3;
}

static void free_entry(const struct CACHED_GENERIC *cached __attribute__((unused)))
{
	freed_entries++;
}

int ntfs_dir_inode_hash(const struct CACHED_GENERIC *cached)
{
	return hash_entry(cached);
}

int ntfs_dir_lookup_hash(const struct CACHED_GENERIC *cached)
{
	return hash_entry(cached);
}

void ntfs_inode_nidata_free(const struct CACHED_GENERIC *cached __attribute__((unused)))
{
	freed_entries++;
}

int ntfs_inode_nidata_hash(const struct CACHED_GENERIC *cached)
{
	return hash_entry(cached);
}

static struct test_cache create_cache(int entry_count, int max_hash,
		cache_hash hash_fn, cache_free free_fn)
{
	size_t entries_size;
	size_t hash_entries_size;
	size_t hash_heads_size;
	size_t total_size;
	unsigned char *mem;
	struct CACHE_HEADER *cache;
	struct CACHED_GENERIC *entry;
	struct HASH_ENTRY *hash_entries;
	struct HASH_ENTRY **hash_heads;

	entries_size = (size_t)entry_count * sizeof(struct test_cache_entry);
	hash_entries_size = max_hash ? (size_t)entry_count * sizeof(struct HASH_ENTRY) : 0;
	hash_heads_size = max_hash ? (size_t)max_hash * sizeof(struct HASH_ENTRY*) : 0;
	total_size = sizeof(struct CACHE_HEADER) + entries_size +
		hash_entries_size + hash_heads_size;

	mem = calloc(1u, total_size);
	TEST_ASSERT(mem != NULL);

	cache = (struct CACHE_HEADER*)(void*)mem;
	cache->name = "unit-cache";
	cache->fixed_size = sizeof(struct test_cache_entry) -
		sizeof(struct CACHED_GENERIC);
	cache->dofree = free_fn;
	cache->dohash = hash_fn;
	cache->max_hash = max_hash;

	entry = &cache->entry[0];
	cache->free_entry = entry;
	for (int i = 0; i < entry_count - 1; i++) {
		struct CACHED_GENERIC *next =
			(struct CACHED_GENERIC*)((char*)entry +
			sizeof(struct test_cache_entry));
		entry->next = next;
		entry = next;
	}

	if (max_hash) {
		hash_entries = (struct HASH_ENTRY*)((char*)&cache->entry[0] +
			entries_size);
		hash_heads = (struct HASH_ENTRY**)((char*)hash_entries +
			hash_entries_size);
		cache->free_hash = hash_entries;
		cache->first_hash = hash_heads;
		for (int i = 0; i < entry_count - 1; i++)
			hash_entries[i].next = &hash_entries[i + 1];
	}

	return (struct test_cache){ .cache = cache, .entry_count = entry_count,
		.max_hash = max_hash };
}

static void destroy_cache(struct test_cache *tc)
{
	struct test_cache_entry *entries;

	entries = (struct test_cache_entry*)(void*)&tc->cache->entry[0];
	for (int i = 0; i < tc->entry_count; i++) {
		if (entries[i].base.varsize) {
			free(entries[i].base.variable);
			entries[i].base.variable = NULL;
		}
	}
	free(tc->cache);
	tc->cache = NULL;
}

static struct test_cache_entry make_item(int key, const char *payload)
{
	struct test_cache_entry item;

	memset(&item, 0, sizeof(item));
	item.key = key;
	item.base.variable = (void*)payload;
	item.base.varsize = payload ? strlen(payload) + 1 : 0;
	return item;
}

static void test_cache_insert_fetch_and_lru(void)
{
	struct test_cache tc = create_cache(2, 4, hash_entry, free_entry);
	struct test_cache_entry item1 = make_item(1, NULL);
	struct test_cache_entry item2 = make_item(2, "two");
	struct test_cache_entry item3 = make_item(3, "three");
	struct CACHED_GENERIC *cached;
	struct test_cache_entry lookup = make_item(1, NULL);

	freed_entries = 0;

	cached = ntfs_enter_cache(tc.cache, &item1.base, compare_entry);
	TEST_ASSERT(cached != NULL);
	TEST_ASSERT_EQ_INT(tc.cache->writes, 1);

	cached = ntfs_enter_cache(tc.cache, &item2.base, compare_entry);
	TEST_ASSERT(cached != NULL);
	TEST_ASSERT_EQ_INT(tc.cache->writes, 2);
	TEST_ASSERT_EQ_INT(strcmp((char*)cached->variable, "two"), 0);

	cached = ntfs_fetch_cache(tc.cache, &lookup.base, compare_entry);
	TEST_ASSERT(cached != NULL);
	TEST_ASSERT_EQ_INT(((struct test_cache_entry*)(void*)cached)->key, 1);
	TEST_ASSERT_EQ_INT(tc.cache->hits, 1);
	TEST_ASSERT_EQ_INT(tc.cache->reads, 1);

	cached = ntfs_enter_cache(tc.cache, &item3.base, compare_entry);
	TEST_ASSERT(cached != NULL);
	TEST_ASSERT_EQ_INT(freed_entries, 1);
	TEST_ASSERT_EQ_INT(((struct test_cache_entry*)(void*)tc.cache->most_recent_entry)->key, 3);
	TEST_ASSERT_EQ_INT(((struct test_cache_entry*)(void*)tc.cache->oldest_entry)->key, 1);

	destroy_cache(&tc);
}

static void test_cache_invalidate_and_remove(void)
{
	struct test_cache tc = create_cache(3, 4, hash_entry, free_entry);
	struct test_cache_entry item1 = make_item(4, "four");
	struct test_cache_entry item2 = make_item(8, "eight");
	struct test_cache_entry item3 = make_item(12, "twelve");
	struct test_cache_entry wanted = make_item(8, NULL);
	struct CACHED_GENERIC *cached;
	int removed;

	freed_entries = 0;
	TEST_ASSERT(ntfs_enter_cache(tc.cache, &item1.base, compare_entry) != NULL);
	TEST_ASSERT(ntfs_enter_cache(tc.cache, &item2.base, compare_entry) != NULL);
	TEST_ASSERT(ntfs_enter_cache(tc.cache, &item3.base, compare_entry) != NULL);

	removed = ntfs_invalidate_cache(tc.cache, &wanted.base, compare_entry, CACHE_FREE);
	TEST_ASSERT_EQ_INT(removed, 1);
	TEST_ASSERT_EQ_INT(freed_entries, 1);

	cached = ntfs_fetch_cache(tc.cache, &wanted.base, compare_entry);
	TEST_ASSERT(cached == NULL);

	cached = tc.cache->most_recent_entry;
	TEST_ASSERT(cached != NULL);
	removed = ntfs_remove_cache(tc.cache, cached, 0);
	TEST_ASSERT_EQ_INT(removed, 1);

	destroy_cache(&tc);
}

static void test_cache_sequential_mode(void)
{
	struct test_cache tc = create_cache(2, 4, hash_entry, NULL);
	struct test_cache_entry item1 = make_item(1, NULL);
	struct test_cache_entry lookup = make_item(1, NULL);
	struct CACHED_GENERIC *cached;

	tc.cache->dohash = NULL;
	cached = ntfs_enter_cache(tc.cache, &item1.base, compare_entry);
	TEST_ASSERT(cached != NULL);

	cached = ntfs_fetch_cache(tc.cache, &lookup.base, compare_entry);
	TEST_ASSERT(cached != NULL);
	TEST_ASSERT_EQ_INT(((struct test_cache_entry*)(void*)cached)->key, 1);

	destroy_cache(&tc);
}

static void test_create_and_free_lru_caches(void)
{
	ntfs_volume vol;

	memset(&vol, 0, sizeof(vol));
	freed_entries = 0;

	ntfs_create_lru_caches(&vol);
	TEST_ASSERT(vol.xinode_cache != NULL);
	TEST_ASSERT(vol.nidata_cache != NULL);
	TEST_ASSERT(vol.lookup_cache != NULL);
	TEST_ASSERT(vol.securid_cache != NULL);
	TEST_ASSERT(vol.legacy_cache != NULL);

	ntfs_free_lru_caches(&vol);
}

int main(void)
{
	TEST_RUN(test_cache_insert_fetch_and_lru);
	TEST_RUN(test_cache_invalidate_and_remove);
	TEST_RUN(test_cache_sequential_mode);
	TEST_RUN(test_create_and_free_lru_caches);
	return 0;
}
