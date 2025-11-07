#!/usr/bin/env bash
set -euo pipefail

pass(){ printf "✅ %s\n" "$1"; }
fail(){ printf "❌ %s\n" "$1"; exit 1; }

# 1) memlayout.h: USYSCALL + struct usyscall, exactly once each
d1=$(grep -c '^[[:space:]]*#define[[:space:]]\+USYSCALL' kernel/memlayout.h || true)
d2=$(grep -c '^[[:space:]]*struct[[:space:]]\+usyscall[[:space:]]*{' kernel/memlayout.h || true)
[[ "$d1" -eq 1 ]] || fail "USYSCALL #define should appear exactly once (found $d1)"
[[ "$d2" -eq 1 ]] || fail "struct usyscall should appear exactly once (found $d2)"
pass "memlayout.h defines USYSCALL and struct usyscall exactly once"

# 2) proc.h: struct proc has void *usyscall
grep -q 'void[[:space:]]*\*usyscall' kernel/proc.h \
  && pass "proc.h has void *usyscall in struct proc" \
  || fail "proc.h missing void *usyscall in struct proc"

# 3) proc.c: helper exists and is defined once
h1=$(grep -n 'write_usys_pid(struct proc \*p)' kernel/proc.c | wc -l)
[[ "$h1" -eq 1 ]] || fail "write_usys_pid helper should be defined exactly once (found $h1)"
pass "write_usys_pid helper present once"

# 4) allocproc(): allocate + zero + write pid
grep -q 'p->usyscall[[:space:]]*=[[:space:]]*kalloc' kernel/proc.c \
  || fail "allocproc() missing: p->usyscall = kalloc()"
grep -q 'memset[[:space:]]*(p->usyscall,[[:space:]]*0,[[:space:]]*PGSIZE' kernel/proc.c \
  || fail "allocproc() missing: memset(p->usyscall, 0, PGSIZE)"
grep -q 'write_usys_pid[[:space:]]*(p)' kernel/proc.c \
  || fail "allocproc() missing: write_usys_pid(p)"
pass "allocproc() allocates, zeros, and writes pid to USYSCALL page"

# 5) proc_pagetable(): maps USYSCALL with PA and correct flags
grep -q 'mappages(.*USYSCALL.*PTE_R[[:space:]]*\|[[:space:]]*PTE_U' kernel/proc.c \
  || fail "proc_pagetable() missing USYSCALL mappages(..., PTE_R | PTE_U)"
grep -q 'mappages(.*USYSCALL.*kvmpa' kernel/proc.c \
  || fail "USYSCALL mapping must use kvmpa(...) (PA), not the kernel VA"
pass "proc_pagetable() maps USYSCALL with kvmpa(...) and PTE_R | PTE_U"

# 6) proc_freepagetable(): unmaps USYSCALL
c6=$(grep -c 'uvmunmap(.*USYSCALL' kernel/proc.c)
[[ "$c6" -ge 1 ]] || fail "proc_freepagetable() should uvmunmap(..., USYSCALL, ...)"
# also ensure proc_freepagetable defined once
pfp=$(grep -n 'proc_freepagetable(.*pagetable_t' kernel/proc.c | wc -l)
[[ "$pfp" -eq 1 ]] || fail "proc_freepagetable() appears $pfp times (should be 1)"
pass "proc_freepagetable() unmaps USYSCALL (and no duplicates)"

# 7) freeproc(): frees the physical USYSCALL page
grep -q 'kfree(p->usyscall)' kernel/proc.c \
  || fail "freeproc() missing kfree(p->usyscall)"
pass "freeproc() frees the USYSCALL page"

# 8) user side: ulib.c has include + ugetpid(); user.h does NOT include memlayout.h
grep -q 'kernel/memlayout.h' user/ulib.c \
  || fail "user/ulib.c should include kernel/memlayout.h"
grep -q '^int[[:space:]]\+ugetpid' user/ulib.c \
  || fail "user/ulib.c missing ugetpid()"
! grep -q 'kernel/memlayout.h' user/user.h \
  && pass "user/user.h does NOT include memlayout.h (good)" \
  || fail "user/user.h should NOT include memlayout.h"

echo "--------------------------------------------------"
echo "All static checks passed. Now build and run pgtbltest."
echo "Inside xv6, you should see ugetpid_test: OK."
