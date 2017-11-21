#!/usr/bin/env bash

repo_src_dir="${HOME}/lin/pr2/impl/parteB"
target_dir="${HOME}"
comp_sources="${HOME}/linux-3.14.1.tar.gz"
sources="${target_dir}/linux-3.14.1"

make_file="${sources}/kernel/Makefile"
syscall_64="${sources}/arch/x86/syscalls/syscall_64.tbl"
syscall_32="${sources}/arch/x86/syscalls/syscall_32.tbl"

# Decompress
tar -xzf "${comp_sources}" -C "${target_dir}"

# Add syscall numbers
sed -i.bak '/^315.*/a 316 common ledctl sys_ledctl' "${syscall_64}"
sed -i.bak '/^352.*/a 353 i386 ledctl sys_ledctl' "${syscall_32}"

# Add file to kernel/ makefile
sed -i.bak -e 's_.* async.o .*_& ledctlsys.o_' "${make_file}"

cp "${repo_src_dir}/ledctlsys.c" "${sources}/kernel/"

cp /boot/config-3.14.1.lin "${sources}/.config"
pushd "${sources}"
make oldconfig

CONCURRENCY_LEVEL=3 make-kpkg --rootcmd fakeroot \
    --initrd --revision=1.0 \
    --append-to-version=".ledctl" \
    kernel_image kernel_headers

popd

sudo dpkg -i linux-image-3.14.1.ledctl_1.0_amd64.deb \
             linux-headers-3.14.1.ledctl_1.0_amd64.deb

echo "Done, please reboot"
