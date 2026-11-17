#!/usr/bin/env bash
set -euo pipefail
probe_repo=$PWD
mkdir -p evidence
git rev-parse HEAD > evidence/commit.txt
python3 - <<'PY'
from pathlib import Path
p = Path('src/filesys/inode.c')
p.write_text(p.read_text() + '''
block_sector_t inode_probe_sector(struct inode* inode, off_t offset) {
  rw_lock_acquire(&inode->growth_lock, RW_READER);
  block_sector_t sector = data_sector(inode->sector, offset / BLOCK_SECTOR_SIZE, NULL);
  rw_lock_release(&inode->growth_lock, RW_READER);
  return sector;
}
''')
p = Path('src/filesys/cache.c')
s = p.read_text().replace('#include <debug.h>', '#include <debug.h>\n#include <stdio.h>')
s = s.replace('#define CACHE_SIZE 64', 'static void cache_probe_disk_read(block_sector_t, void*);\n#define CACHE_SIZE 64')
marker = 'block_read(fs_device, sector, victim->data);'
assert s.count(marker) == 1
s = s.replace(marker, 'cache_probe_disk_read(sector, victim->data);')
s += '''
static struct semaphore probe_first, probe_second;
static block_sector_t probe_a, probe_b;
static bool probe_armed;
void cache_probe_arm(block_sector_t a, block_sector_t b) {
  ASSERT(a && b && a != b);
  sema_init(&probe_first, 0);
  sema_init(&probe_second, 0);
  probe_a = a;
  probe_b = b;
  probe_armed = true;
  printf("IO GATE sectors %u %u\\n", a, b);
}
static void cache_probe_disk_read(block_sector_t sector, void* data) {
  if (probe_armed && sector == probe_a) {
    printf("IO A waiting\\n");
    sema_up(&probe_first);
    sema_down(&probe_second);
    printf("IO A resumed\\n");
  } else if (probe_armed && sector == probe_b) {
    sema_down(&probe_first);
    printf("IO B progressed\\n");
    probe_armed = false;
    sema_up(&probe_second);
  }
  block_read(fs_device, sector, data);
}
'''
p.write_text(s)
p = Path('src/userprog/syscall.c')
s = p.read_text()
marker = 'f->eax = args[1] + 1u;'
assert s.count(marker) == 1
s = s.replace(marker, '''extern block_sector_t inode_probe_sector(struct inode*, off_t);
      extern void cache_probe_arm(block_sector_t, block_sector_t);
      struct descriptor* probe_first_fd = descriptor(args[1] & 65535);
      struct descriptor* probe_second_fd = descriptor(args[1] >> 16);
      ASSERT(probe_first_fd && probe_second_fd);
      cache_probe_arm(inode_probe_sector(file_get_inode(probe_first_fd->file), 0),
                      inode_probe_sector(file_get_inode(probe_second_fd->file), 512));
      f->eax = 0;''')
p.write_text(s)
PY
git diff -- src/filesys/inode.c src/filesys/cache.c src/userprog/syscall.c > evidence/probe.patch
export PATH="$PWD/src/utils:$PATH"
chmod +x src/utils/pintos*
make -C src/utils setitimer-helper
make -C src/filesys > evidence/build-positive.txt 2>&1
cd src/filesys/build
for mode in same different; do
  pintos -v -k -T 40 --qemu --filesys-size=2 -p tests/filesys/extended/io-gate -a io-gate -- -q -f run "io-gate $mode" > "../../../evidence/positive-$mode.output" 2>&1
  python3 - "$mode" <<'PY'
from pathlib import Path
import sys
mode = sys.argv[1]
p = Path('../../../evidence') / ('positive-' + mode + '.output')
s = p.read_text()
assert s.index('IO A waiting') < s.index('IO B progressed') < s.index('IO A resumed')
assert 'IO gate ' + mode + ' PASS' in s and s.count('io-gate: exit(0)') == 3
assert 'PANIC' not in s and 'TIMEOUT' not in s and 'Powering off' in s
p.with_suffix('.result').write_text('PASS\n')
PY
done
cd "$probe_repo"
python3 - <<'PY'
from pathlib import Path
p = Path('src/filesys/file.c')
s = p.read_text()
marker = 'struct file {'
assert s.count(marker) == 1
s = s.replace(marker, 'static struct lock serialized_read;\nstatic bool serialized_ready;\n' + marker)
marker = 'struct file* file_open(struct inode* inode) {'
assert s.count(marker) == 1
s = s.replace(marker, marker + '''
  if (!serialized_ready) {
    lock_init(&serialized_read);
    serialized_ready = true;
  }
''')
start = s.index('off_t file_read(')
end = s.index('off_t file_read_at(', start)
body = s[start:end]
assert body.count('  return bytes_read;') == 1
body = body.replace('  lock_acquire(&file->position_lock);', '  lock_acquire(&serialized_read);\n  lock_acquire(&file->position_lock);')
body = body.replace('  return bytes_read;', '  lock_release(&serialized_read);\n  return bytes_read;')
p.write_text(s[:start] + body + s[end:])
PY
git diff -- src/filesys/file.c > evidence/serialization.patch
make -C src/filesys > evidence/build-negative.txt 2>&1
cd src/filesys/build
for mode in same different; do
  set +e
  pintos -v -k -T 12 --qemu --filesys-size=2 -p tests/filesys/extended/io-gate -a io-gate -- -q -f run "io-gate $mode" > "../../../evidence/negative-$mode.output" 2>&1
  probe_status=$?
  set -e
  python3 - "$mode" "$probe_status" <<'PY'
from pathlib import Path
import sys
mode, status = sys.argv[1], int(sys.argv[2])
s = (Path('../../../evidence') / ('negative-' + mode + '.output')).read_text()
# src/utils/pintos deliberately exits 0 after its wall-clock timeout.
# Require the diagnostic and blocked progress, rather than treating exit 0 as success.
assert status == 0 and 'TIMEOUT after' in s and 'IO A waiting' in s
assert 'worker B ready to read' in s and 'IO B progressed' not in s and 'PANIC' not in s
assert 'Powering off' not in s and 'IO gate ' + mode + ' PASS' not in s
(Path('../../../evidence') / ('negative-' + mode + '.txt')).write_text('EXPECTED TIMEOUT: global read serialization detected\n')
PY
done
