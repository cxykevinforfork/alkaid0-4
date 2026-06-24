PRAGMA journal_mode = WAL;

PRAGMA foreign_keys = ON;

PRAGMA synchronous = NORMAL;

-- 缓存大小：默认 -2000（约 8MB，2000 页 × 4KB），
-- 降为 -512 后仅 ~2MB（128 页 × 4KB），静默时节省约 6MB
PRAGMA cache_size = -512;

-- mmap_size = 2MB，保留少量内存映射加速查询但不过度占用虚拟内存
PRAGMA mmap_size = 2000000;

PRAGMA page_size = 4096;

VACUUM;

ANALYZE;