#!/usr/bin/env bash
set -euo pipefail

output_image="${1:?output image is required}"
kernel="${2:?kernel path is required}"
grub_mkimage="${3:?grub-mkimage path is required}"
grub_root="$(dirname "$grub_mkimage")"
grub_efi_mod_dir="$grub_root/x86_64-efi"
mformat="${GRUB_MFORMAT:-mformat}"
mmd="${GRUB_MMD:-mmd}"
mcopy="${GRUB_MCOPY:-mcopy}"

if [ "$mformat" = "mformat" ] && [ -x "/c/msys64/mingw64/bin/mformat.exe" ]; then
  mformat="/c/msys64/mingw64/bin/mformat.exe"
  mmd="/c/msys64/mingw64/bin/mmd.exe"
  mcopy="/c/msys64/mingw64/bin/mcopy.exe"
fi

for tool in "$grub_mkimage" "$mformat" "$mmd" "$mcopy"; do
  if ! command -v "$tool" >/dev/null 2>&1 && [ ! -x "$tool" ]; then
    echo "Required UEFI USB tool not found: $tool" >&2
    exit 1
  fi
done

if [ ! -d "$grub_efi_mod_dir" ]; then
  echo "GRUB EFI modules not found: $grub_efi_mod_dir" >&2
  exit 1
fi

stage_dir="$(mktemp -d)"
trap 'rm -rf "$stage_dir"' EXIT

mkdir -p "$stage_dir/EFI/BOOT" "$stage_dir/boot/grub/x86_64-efi"
cp "$kernel" "$stage_dir/boot/kernel.elf"
cp -a "$grub_efi_mod_dir/." "$stage_dir/boot/grub/x86_64-efi/"

cat > "$stage_dir/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
set gfxmode=text
terminal_input console
terminal_output console
insmod multiboot2
multiboot2 /boot/kernel.elf
boot
EOF

cat > "$stage_dir/early.cfg" <<'EOF'
set gfxmode=1024x768
set gfxpayload=keep
terminal_output gfxterm
search --file /boot/kernel.elf --set=root
multiboot2 /boot/kernel.elf framebuffer
boot
EOF

"$grub_mkimage" -O x86_64-efi \
  -o "$stage_dir/EFI/BOOT/BOOTX64.EFI" \
  -p /boot/grub \
  -c "$stage_dir/early.cfg" \
  part_gpt part_msdos fat search search_fs_file efi_gop video video_fb terminal gfxterm font multiboot2

rm -f "$output_image"
dd if=/dev/zero of="$output_image" bs=1M count=64 2>/dev/null
"$mformat" -i "$output_image" -F -v SMIGGLES ::
"$mmd" -i "$output_image" ::/EFI ::/EFI/BOOT ::/boot ::/boot/grub ::/boot/grub/x86_64-efi
"$mcopy" -s -i "$output_image" "$stage_dir/EFI" ::/
"$mcopy" -s -i "$output_image" "$stage_dir/boot" ::/

echo "Created UEFI FAT32 image: $output_image"
