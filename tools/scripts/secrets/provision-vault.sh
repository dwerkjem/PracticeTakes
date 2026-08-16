#!/usr/bin/env bash
# Provision an SSH-unlockable LUKS recovery stick, and load this project's
# secrets onto it.
#
# The stick is bootstrap and cold storage, not daily transport. Day to day, SOPS
# keeps secrets encrypted in the working tree and an age identity derived from
# your SSH key decrypts them. This stick is what rebuilds that from nothing.
#
#   p1  64M  vfat, plain   -- marker + the LUKS keyfile, age-wrapped to your SSH key
#   p2  rest LUKS2 -> ext4 -- everything else
#
# Two keyslots, and both are load-bearing:
#   slot 0  passphrase  -- the bootstrap path. A new machine has no SSH key, so
#                          it cannot use slot 1; it opens with this, takes the
#                          SSH key off the stick, and proceeds from there.
#   slot 1  keyfile     -- the routine path, unwrapped with the SSH key you have.
#
# The drive is locked and closed on every exit, including failure and Ctrl-C.
#
#   provision-vault.sh                      pick a device interactively, erase, provision
#   provision-vault.sh --device /dev/sdX    skip the picker
#   provision-vault.sh --refresh            reload secrets onto an existing stick
#
# Run it, do not paste it into a shell.

set -euo pipefail

VAULT_MARKER=.secrets-vault
MOUNT=/mnt/secrets
MAPPER=secrets
KEYFILE_BYTES=4096
SCRATCH=/dev/shm/vault-provision

# A repository is optional. Without one the stick is still provisioned and the
# bootstrap keys are still copied -- only the per-project SOPS secrets are
# skipped. That keeps the script usable for making a recovery stick anywhere.
PROJECT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"

# Derived from the remote, so two clones under different local directory names
# resolve to the same secrets.
if [ -n "$PROJECT_ROOT" ]; then
  PROJECT="$(basename -s .git "$(git -C "$PROJECT_ROOT" remote get-url origin 2>/dev/null || echo "$PROJECT_ROOT")")"
else
  PROJECT=""
fi

# A recovery stick is realistically 4-128 GB. Anything much larger that still
# reports itself removable is usually an external drive in a USB enclosure --
# the one dangerous case the removable check cannot distinguish.
WARN_SIZE_BYTES=$((256 * 1024 * 1024 * 1024))

SSH_KEY="${VAULT_SSH_IDENTITY:-$HOME/.ssh/id_ed25519}"
SSH_PUB="$SSH_KEY.pub"

# Secrets living outside the repository. "<absolute source>:<name on stick>"
PERSONAL_SECRETS=(
  "$SSH_KEY:id_ed25519"
  "$SSH_PUB:id_ed25519.pub"
  "$HOME/.config/sops/age/keys.txt:sops-age-key.txt"
)

MODE=provision
DEV=""
REREGISTER=0
while (( $# )); do
  case "$1" in
    --refresh)    MODE=refresh ;;
    --device)     DEV="${2:?--device needs a path}"; shift ;;
    --reregister) REREGISTER=1 ;;
    --help|-h)    sed -n '2,32p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1"; exit 1 ;;
  esac
  shift
done

# ==========================================================================
# Automatic locking, armed the moment the volume opens.

TRAP_ARMED=0
cleanup() {
  local status=$?
  [ "$TRAP_ARMED" -eq 1 ] || exit $status
  echo; echo ">>> locking the drive"
  sync
  mountpoint -q "$MOUNT" && sudo umount "$MOUNT" 2>/dev/null || true
  [ -e "/dev/mapper/$MAPPER" ] && sudo cryptsetup close "$MAPPER" 2>/dev/null || true
  rm -rf "$SCRATCH" 2>/dev/null || true
  if [ -e "/dev/mapper/$MAPPER" ]; then
    echo "    WARNING: mapping still open. Run:"
    echo "      sudo umount $MOUNT; sudo cryptsetup close $MAPPER"
  else
    echo "    locked. Safe to unplug."
  fi
  exit $status
}
trap cleanup EXIT INT TERM
arm() { TRAP_ARMED=1; }

# ==========================================================================
# Device safety.
#
# Three independent gates, because the cost of getting this wrong is somebody's
# whole disk. Note a USB-attached SATA SSD reports removable=0, so the removable
# check alone already excludes external drives that merely look like sticks.

system_disks() {
  # The disks backing /, /boot/efi and swap, whatever they are called.
  { findmnt -no SOURCE / /boot /boot/efi 2>/dev/null
    awk 'NR>1 {print $1}' /proc/swaps 2>/dev/null; } \
    | while read -r src; do lsblk -no PKNAME "$src" 2>/dev/null; done | sort -u
}

is_removable() {
  [ "$(cat "/sys/block/$1/removable" 2>/dev/null)" = "1" ]
}

assert_safe_target() {
  local dev="$1" name; name="$(basename "$dev")"

  [ -b "$dev" ] || { echo "not a block device: $dev"; exit 1; }
  [ -e "/sys/block/$name" ] || { echo "$dev is a partition; give the whole disk"; exit 1; }

  if ! is_removable "$name"; then
    echo "REFUSING $dev: not a removable device."
    echo "Externally attached SSDs report removable=0 and are excluded on purpose."
    exit 1
  fi
  if system_disks | grep -qx "$name"; then
    echo "REFUSING $dev: it backs /, /boot or swap."
    exit 1
  fi
}

describe() {
  local dev="$1"
  lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT,MODEL,SERIAL "$dev"
}

# ==========================================================================
# Registration.
#
# Each developer keeps one stick dedicated to this. Recording its serial means
# every later run can prove it is talking to that stick and not to whatever
# happened to enumerate as /dev/sdc this boot. The file is per-user rather than
# in the repository, because the serial identifies one person's hardware and
# committing it would be both wrong and useless to everybody else.

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/practice-takes"
CONFIG="$CONFIG_DIR/vault.conf"

config_get() { sed -n "s/^ *$1 *= *//p" "$CONFIG" 2>/dev/null | head -1; }

write_registration() {
  local dev="$1"
  mkdir -p "$CONFIG_DIR"; chmod 700 "$CONFIG_DIR"
  cat > "$CONFIG" <<EOF
# The recovery stick this machine is registered to.
# Written by tools/scripts/secrets/provision-vault.sh. Re-run with
# --reregister to point at a different stick.
serial = $(lsblk -dno SERIAL "$dev" | xargs)
model = $(lsblk -dno MODEL "$dev" | xargs)
size = $(lsblk -dno SIZE "$dev" | xargs)
EOF
  chmod 600 "$CONFIG"
  echo "registered this stick in $CONFIG"
}

# The disk currently attached whose serial matches the registration, if any.
registered_device() {
  local want; want="$(config_get serial)"
  [ -n "$want" ] || return 1
  local disk
  for d in /sys/block/*/; do
    disk="$(basename "$d")"
    case "$disk" in loop*|ram*|dm-*|sr*) continue;; esac
    [ "$(lsblk -dno SERIAL "/dev/$disk" 2>/dev/null | xargs)" = "$want" ] \
      && { echo "/dev/$disk"; return 0; }
  done
  return 1
}

# Refuse to erase a stick other than the registered one unless told explicitly.
assert_registered_or_ask() {
  local dev="$1" want; want="$(config_get serial)"
  local have; have="$(lsblk -dno SERIAL "$dev" | xargs)"

  if [ -z "$want" ]; then
    echo
    echo "No stick is registered on this machine yet."
    echo "This one will become the registered recovery stick for $USER:"
    describe "$dev" | sed 's/^/    /'
    echo
    printf 'Is this the stick you keep dedicated to this project? [y/N] '
    read -r go; [ "$go" = "y" ] || { echo "aborted"; exit 1; }
    return 0
  fi

  if [ "$have" = "$want" ]; then
    echo "device matches the stick registered in $CONFIG ✓"
    return 0
  fi

  echo
  echo "  ################ THIS IS NOT YOUR REGISTERED STICK ################"
  echo "  registered : $(config_get model) $(config_get size)  serial …${want: -8}"
  echo "  this device: $(lsblk -dno MODEL "$dev" | xargs) $(lsblk -dno SIZE "$dev" | xargs)  serial …${have: -8}"
  echo "  ###################################################################"
  echo
  echo "Re-run with --reregister if you genuinely mean to switch sticks."
  exit 1
}

pick_device() {
  local -a candidates=()
  for d in /sys/block/*/; do
    local n; n="$(basename "$d")"
    case "$n" in loop*|ram*|dm-*|sr*) continue;; esac
    is_removable "$n" || continue
    system_disks | grep -qx "$n" && continue
    candidates+=("/dev/$n")
  done

  (( ${#candidates[@]} )) || { echo "no removable disks found"; exit 1; }

  echo "Removable disks:"
  local i=1
  for c in "${candidates[@]}"; do
    printf '\n[%d] %s\n' "$i" "$c"
    describe "$c" | sed 's/^/    /'
    i=$((i + 1))
  done
  echo
  printf 'Choose a number (or Ctrl-C): '
  read -r choice
  [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#candidates[@]} )) \
    || { echo "not a valid choice"; exit 1; }
  DEV="${candidates[$((choice - 1))]}"
}

# Signals that a "removable" disk is probably not a blank thumb drive. None is
# conclusive on its own, which is why they escalate the confirmation rather than
# refusing outright -- a legitimately large or populated stick still exists.
risk_warnings() {
  local dev="$1" bytes parts mounted used label
  bytes="$(lsblk -dnbo SIZE "$dev" 2>/dev/null || echo 0)"

  if [ "$bytes" -ge "$WARN_SIZE_BYTES" ]; then
    echo "size is $(lsblk -dno SIZE "$dev" | xargs) — far larger than a recovery stick; an external drive in a USB enclosure looks identical to this script"
  fi

  parts="$(lsblk -lno NAME "$dev" | tail -n +2 | wc -l)"
  [ "$parts" -gt 2 ] && echo "$parts partitions — more than this script creates, so it holds someone's layout"

  mounted="$(lsblk -lno MOUNTPOINT "$dev" | grep -v '^$' | paste -sd, -)"
  [ -n "$mounted" ] && echo "currently mounted at: $mounted"

  # Data actually present, rather than merely formatted.
  while read -r mp; do
    [ -n "$mp" ] || continue
    used="$(df -B1 --output=used "$mp" 2>/dev/null | tail -1 | xargs)"
    [ -n "$used" ] && [ "$used" -gt $((1024 * 1024 * 1024)) ] \
      && echo "$mp holds $(numfmt --to=iec "$used" 2>/dev/null || echo "$used bytes") of data"
  done < <(lsblk -lno MOUNTPOINT "$dev" | grep -v '^$')

  label="$(lsblk -lno LABEL "$dev" | grep -v '^$' | grep -vE '^(VAULT|VAULTBOOT)$' | paste -sd, -)"
  [ -n "$label" ] && echo "existing filesystem labels: $label — someone named these"
}

confirm_destruction() {
  local dev="$1" size serial
  size="$(lsblk -dno SIZE "$dev" | xargs)"
  serial="$(lsblk -dno SERIAL "$dev" | xargs)"

  echo
  echo "About to ERASE $dev — everything on it is destroyed."
  describe "$dev" | sed 's/^/    /'

  local -a warnings=()
  mapfile -t warnings < <(risk_warnings "$dev")

  if (( ${#warnings[@]} )); then
    echo
    echo "  ################ THIS DOES NOT LOOK LIKE A BLANK STICK ################"
    for w in "${warnings[@]}"; do echo "  !!  $w"; done
    echo "  #######################################################################"
    echo
    # Escalated: the serial cannot be guessed or typed from habit, so it forces
    # the operator to read the specific device in front of them.
    if [ -z "$serial" ]; then
      printf 'No serial reported. Type the full device path %s to continue: ' "$dev"
      read -r reply
      [ "$reply" = "$dev" ] || { echo "aborted"; exit 1; }
    else
      printf 'Type the last 8 characters of its serial (%s) to continue: ' "…${serial: -8}"
      read -r reply
      [ "$reply" = "${serial: -8}" ] || { echo "aborted"; exit 1; }
    fi
  fi

  echo
  # Two independent tokens. A fixed word alone becomes muscle memory; the size
  # is specific to the device on screen, so typing it means the row was read.
  printf 'Type "ERASE %s" to continue: ' "$size"
  read -r reply
  [ "$reply" = "ERASE $size" ] || { echo "aborted"; exit 1; }
}

# ==========================================================================

preflight() {
  command -v age >/dev/null || { echo "age is not on PATH"; exit 1; }
  [ -f "$SSH_KEY" ] && [ -f "$SSH_PUB" ] || { echo "no SSH keypair at $SSH_KEY"; exit 1; }

  if ssh-keygen -y -P "" -f "$SSH_KEY" >/dev/null 2>&1; then
    echo
    echo "!!! $SSH_KEY has NO PASSPHRASE."
    echo "!!! It unlocks this vault and derives the SOPS age identity, so anyone"
    echo "!!! who reads the file reads every secret. Fix with: ssh-keygen -p -f $SSH_KEY"
    echo
    printf 'Continue anyway? [y/N] '
    read -r go; [ "$go" = "y" ] || exit 1
  fi
}

provision() {
  # Prefer the stick already registered to this machine, so the common case
  # needs no picker and no chance to choose wrongly.
  if [ -z "$DEV" ] && [ "$REREGISTER" -eq 0 ] && DEV="$(registered_device)"; then
    echo "found the registered stick at $DEV"
  elif [ -z "$DEV" ]; then
    if [ -n "$(config_get serial)" ] && [ "$REREGISTER" -eq 0 ]; then
      echo "The registered stick (serial …$(config_get serial | tail -c 9)) is not connected."
      echo "Plug it in, or pass --reregister to adopt a different one."
      exit 1
    fi
    pick_device
  fi

  assert_safe_target "$DEV"
  [ "$REREGISTER" -eq 1 ] || assert_registered_or_ask "$DEV"
  confirm_destruction "$DEV"
  write_registration "$DEV"

  sudo umount "$MOUNT" 2>/dev/null || true
  sudo cryptsetup close "$MAPPER" 2>/dev/null || true
  sudo umount "$DEV"?* 2>/dev/null || true

  sudo wipefs -a "$DEV"
  sudo sgdisk --zap-all "$DEV"
  sudo sgdisk --new=1:0:+64M --typecode=1:0700 --change-name=1:VAULTBOOT "$DEV"
  sudo sgdisk --new=2:0:0    --typecode=2:8309 --change-name=2:VAULT     "$DEV"
  sudo partprobe "$DEV"; sleep 1

  local p1 p2
  p1="$(lsblk -lno NAME "$DEV" | sed -n 2p)"; p1="/dev/$p1"
  p2="$(lsblk -lno NAME "$DEV" | sed -n 3p)"; p2="/dev/$p2"

  sudo mkfs.vfat -n VAULTBOOT "$p1"

  echo
  echo ">>> Set the RECOVERY passphrase — how a brand-new machine gets in."
  echo ">>> Write it down somewhere physical."
  sudo cryptsetup luksFormat --type luks2 \
    --cipher aes-xts-plain64 --key-size 512 \
    --pbkdf argon2id --iter-time 5000 "$p2"

  mkdir -p "$SCRATCH"; chmod 700 "$SCRATCH"
  head -c "$KEYFILE_BYTES" /dev/urandom > "$SCRATCH/keyfile"

  echo
  echo ">>> That passphrase once more, to add the SSH keyslot."
  # Full-entropy random bytes need no key stretching, so this slot skips the
  # expensive KDF the passphrase requires. Keeps SSH unlocks instant.
  sudo cryptsetup luksAddKey --pbkdf pbkdf2 --pbkdf-force-iterations 1000 \
    "$p2" "$SCRATCH/keyfile"

  # Prove BOTH paths before the plaintext keyfile is destroyed. Getting this
  # order wrong produces a stick nobody can open.
  sudo cryptsetup open --key-file="$SCRATCH/keyfile" --keyfile-size="$KEYFILE_BYTES" \
    "$p2" vaultcheck
  sudo cryptsetup close vaultcheck
  age -R "$SSH_PUB" -o "$SCRATCH/keyfile.age" "$SCRATCH/keyfile"
  age -d -i "$SSH_KEY" "$SCRATCH/keyfile.age" | cmp -s - "$SCRATCH/keyfile" \
    || { echo "SSH round-trip FAILED — aborting"; exit 1; }
  echo "both unlock paths verified"

  sudo cryptsetup open --key-file="$SCRATCH/keyfile" --keyfile-size="$KEYFILE_BYTES" \
    "$p2" "$MAPPER"
  arm
  sudo mkfs.ext4 -q -L VAULT "/dev/mapper/$MAPPER"
  sudo mkdir -p "$MOUNT"
  sudo mount "/dev/mapper/$MAPPER" "$MOUNT"
  sudo chown "$(id -u):$(id -g)" "$MOUNT"
  sudo chmod 700 "$MOUNT"

  local BOOT LUKS_UUID SERIAL
  BOOT="$(mktemp -d)"; sudo mount "$p1" "$BOOT"
  LUKS_UUID="$(sudo blkid -s UUID -o value "$p2")"
  SERIAL="$(lsblk -dno SERIAL "$DEV" | xargs)"
  sudo cp "$SCRATCH/keyfile.age" "$BOOT/keyfile.age"
  sudo cp "$SSH_PUB" "$BOOT/recipients.txt"
  { printf 'luks_uuid = %s\n' "$LUKS_UUID"
    printf 'device_serial = %s\n' "$SERIAL"; } | sudo tee "$BOOT/$VAULT_MARKER" >/dev/null
  sudo mkdir -p "$BOOT/bin"
  sudo sync; sudo umount "$BOOT"; rmdir "$BOOT"

  shred -u "$SCRATCH"/keyfile* 2>/dev/null || true
  echo "provisioned $DEV — LUKS $LUKS_UUID"
}

# Identity is the marker file, not a device node: /dev/sdX shifts between boots,
# so anything keyed on the path is wrong by the next reboot. Two passes --
# already-mounted volumes first, then unmounted removable partitions, which are
# probed and unmounted again if they turn out not to be it.
SCAN_MOUNT=""

find_bootstrap() {
  local src tgt
  while read -r src tgt; do
    case "$tgt" in /media/*|/run/media/*|/mnt/*) ;; *) continue;; esac
    [ -f "$tgt/$VAULT_MARKER" ] && { echo "$tgt"; return 0; }
  done < <(awk '$1 ~ /^\/dev\// {print $1, $2}' /proc/mounts)

  local probe; probe="$(mktemp -d)"
  local disk part
  for d in /sys/block/*/; do
    disk="$(basename "$d")"
    case "$disk" in loop*|ram*|dm-*|sr*) continue;; esac
    is_removable "$disk" || continue
    system_disks | grep -qx "$disk" && continue

    while read -r part; do
      [ -n "$part" ] || continue
      grep -q "^/dev/$part " /proc/mounts && continue
      if sudo mount -o ro "/dev/$part" "$probe" 2>/dev/null; then
        if [ -f "$probe/$VAULT_MARKER" ]; then
          sudo umount "$probe"
          sudo mount "/dev/$part" "$probe"   # remount rw for the caller
          SCAN_MOUNT="$probe"
          echo "$probe"; return 0
        fi
        sudo umount "$probe" 2>/dev/null || true
      fi
    done < <(lsblk -lno NAME "/dev/$disk" | tail -n +2)
  done
  rmdir "$probe" 2>/dev/null || true
  return 1
}

unlock_existing() {
  mountpoint -q "$MOUNT" && { arm; return 0; }

  local BOOT own=0
  if ! BOOT="$(find_bootstrap)"; then
    [ -n "$DEV" ] || {
      echo "No vault found. Plug in the stick, or pass --device /dev/sdX."
      echo "A vault is any removable volume with a $VAULT_MARKER file at its root."
      exit 1
    }
    assert_safe_target "$DEV"
    BOOT="$(mktemp -d)"; own=1
    sudo mount "${DEV}1" "$BOOT"
  fi
  # find_bootstrap mounts its own probe directory when it has to go looking.
  [ -n "$SCAN_MOUNT" ] && own=1

  local LUKS_UUID MARKER_SERIAL WANT
  LUKS_UUID="$(sed -n 's/^ *luks_uuid *= *//p' "$BOOT/$VAULT_MARKER")"
  [ -n "$LUKS_UUID" ] || { echo "$BOOT/$VAULT_MARKER does not set luks_uuid"; exit 1; }

  # Three-way agreement: what this machine registered, what the stick says it
  # is, and which volume is about to be opened. Reading is harmless, so a
  # mismatch warns rather than refusing -- but it should never happen quietly.
  MARKER_SERIAL="$(sed -n 's/^ *device_serial *= *//p' "$BOOT/$VAULT_MARKER")"
  WANT="$(config_get serial)"
  if [ -n "$WANT" ] && [ -n "$MARKER_SERIAL" ] && [ "$WANT" != "$MARKER_SERIAL" ]; then
    echo "!!! This stick reports serial …${MARKER_SERIAL: -8} but $CONFIG registered …${WANT: -8}."
    echo "!!! Continuing read-only would be safe, but you are about to write secrets to it."
    printf 'Continue? [y/N] '
    read -r go; [ "$go" = "y" ] || exit 1
  fi

  # Unwrapped in RAM and piped straight in; the key never lands on a filesystem.
  age -d -i "$SSH_KEY" "$BOOT/keyfile.age" \
    | sudo cryptsetup open --key-file=- --keyfile-size="$KEYFILE_BYTES" \
        "/dev/disk/by-uuid/$LUKS_UUID" "$MAPPER"
  arm
  [ "$own" -eq 1 ] && { sudo umount "$BOOT"; rmdir "$BOOT"; }

  sudo mkdir -p "$MOUNT"
  sudo mount "/dev/mapper/$MAPPER" "$MOUNT"
  sudo chown "$(id -u):$(id -g)" "$MOUNT"
  echo "unlocked with your SSH key"
}

looks_encrypted() {
  grep -qE 'ENC\[AES256_GCM|sops_version|"sops"[[:space:]]*:' "$1" 2>/dev/null
}

load_secrets() {
  local BOOTSTRAP="$MOUNT/bootstrap"
  local failures=0

  mkdir -p "$BOOTSTRAP"
  chmod 700 "$BOOTSTRAP"

  echo; echo ">>> bootstrap keys"
  for entry in "${PERSONAL_SECRETS[@]}"; do
    local src="${entry%%:*}" name="${entry##*:}"
    [ -f "$src" ] || { echo "    absent, skipped: $src"; continue; }
    install -m 600 "$src" "$BOOTSTRAP/$name"
    cmp -s "$src" "$BOOTSTRAP/$name" && echo "    ok  $name" \
      || { echo "    VERIFY FAILED $name"; failures=$((failures + 1)); }
  done

  if [ -z "$PROJECT_ROOT" ]; then
    echo
    echo ">>> not inside a repository — bootstrap keys only, no project secrets"
    write_readme
    sync
    [ "$failures" -eq 0 ] && echo "bootstrap keys loaded and verified" || return 1
    return 0
  fi

  local ENC="$MOUNT/projects/$PROJECT/encrypted"
  local PLAIN="$MOUNT/projects/$PROJECT/plaintext"
  mkdir -p "$ENC" "$PLAIN"
  chmod 700 "$PLAIN"

  echo
  # Push the working tree INTO the mirrors, never the reverse. After a rotation
  # the plaintext on disk is the newest thing that exists, and `decrypt` here
  # would overwrite freshly rotated values with stale ciphertext. `encrypt` can
  # only lose an out-of-date mirror, which is the safe direction to fail in.
  echo ">>> syncing SOPS mirrors from the working tree"
  if ( cd "$PROJECT_ROOT" && python3 tools/scripts/secrets/secrets_manager.py encrypt ); then
    echo "    mirrors up to date"
  else
    echo "    *** encrypt FAILED — encrypted/ may not match plaintext/"
    failures=$((failures + 1))
  fi

  echo; echo ">>> project secrets (.sops for normal restore, plaintext as last resort)"
  while IFS= read -r mirror; do
    local plain="${mirror#.secrets/}"; plain="${plain%.sops}"
    install -D -m 600 "$PROJECT_ROOT/$mirror" "$ENC/$plain.sops"

    if [ ! -f "$PROJECT_ROOT/$plain" ]; then
      echo "    $plain — encrypted only, no plaintext on disk"; continue
    fi
    if looks_encrypted "$PROJECT_ROOT/$plain"; then
      echo "    STILL ENCRYPTED, not filed as plaintext: $plain"
      failures=$((failures + 1)); continue
    fi
    install -D -m 600 "$PROJECT_ROOT/$plain" "$PLAIN/$plain"
    cmp -s "$PROJECT_ROOT/$plain" "$PLAIN/$plain" && echo "    ok  $plain" \
      || { echo "    VERIFY FAILED $plain"; failures=$((failures + 1)); }
  done < <(cd "$PROJECT_ROOT" && find .secrets -type f -name '*.sops' 2>/dev/null | sort)

  write_readme
  sync
  echo
  if [ "$failures" -gt 0 ]; then
    echo "FINISHED WITH $failures PROBLEM(S) — read the log. Nothing was deleted."
    return 1
  fi
  echo "all secrets loaded and verified"
}

write_readme() {
  cat > "$MOUNT/README.txt" <<'EOF'
Recovery stick — rebuilding from nothing
========================================

Unlock: a machine that already has the SSH key unlocks automatically. A NEW
machine cannot, so use the LUKS recovery passphrase and restore the SSH key
first -- everything else depends on it.

  1. bootstrap/id_ed25519      -> ~/.ssh/id_ed25519      (chmod 600)
     bootstrap/id_ed25519.pub  -> ~/.ssh/id_ed25519.pub  (chmod 644)

  2. bootstrap/sops-age-key.txt -> ~/.config/sops/age/keys.txt  (chmod 600)

     Or derive it from the SSH key instead. Note -stdinpass: without it
     ssh-to-age refuses a passphrase-protected key outright.
       ssh-to-age -private-key -stdinpass -i ~/.ssh/id_ed25519

  3. Clone the repo, then:
       python3 tools/scripts/secrets/secrets_manager.py decrypt

projects/<name>/encrypted/   copies of .secrets/*.sops (need the age key)
projects/<name>/plaintext/   the same secrets in the clear, if SOPS itself fails

Everything here is protected by LUKS and nothing else. Treat a mounted stick
exactly as you would the secrets themselves.

If this stick is lost: rotate every credential it held. They were chosen to be
rotatable. The SSH key is the exception -- replace it on GitHub and re-encrypt
the SOPS mirrors to the new derived recipient.
EOF
}

# ==========================================================================

preflight
if [ "$MODE" = provision ]; then provision; else unlock_existing; fi
load_secrets || true

echo; echo "Contents:"
find "$MOUNT" -type f -printf '  %-58p %s bytes\n' 2>/dev/null | sed "s|$MOUNT/||"
echo
echo "Once, if you have not already:"
echo "  sudo cryptsetup luksHeaderBackup <luks partition> --header-backup-file ~/vault-luks-header.img"
echo "  Keep it OFF the stick — a corrupt header loses the volume even with the key."
