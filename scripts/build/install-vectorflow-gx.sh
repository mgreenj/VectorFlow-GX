#!/usr/bin/env bash

REPO_URL=""
BRANCH="main"
BUILD_DIR="./VectorFlow-GX"
NIC_PCI=""
GPU_PCI=""
DO_CLONE=true
DO_BIND=true
CUDA_HOME="${CUDA_HOME:-/usr/local/cuda-13.1}"
DPDK_PREFIX="${DPDK_PREFIX:-/usr/local}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()  { echo -e "${GREEN}[+]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }


usage() {
    grep '^#' "$0" | grep -v '#!/' | sed 's/^# \?//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -r) REPO_URL="$2";  shift 2 ;;
        -b) BRANCH="$2";    shift 2 ;;
        -d) BUILD_DIR="$2"; shift 2 ;;
        -n) NIC_PCI="$2";   shift 2 ;;
        -g) GPU_PCI="$2";   shift 2 ;;
        --no-clone) DO_CLONE=false; shift ;;
        --no-bind)  DO_BIND=false;  shift ;;
        -h) usage ;;
        *) err "Unknown option: $1" ;;
    esac
done


log "Checking prerequisites..."

command -v meson       >/dev/null 2>&1 || err "meson not found"
command -v ninja       >/dev/null 2>&1 || err "ninja not found"
command -v pkg-config  >/dev/null 2>&1 || err "pkg-config not found"
command -v nvidia-smi  >/dev/null 2>&1 || err "nvidia-smi not found - NVIDIA drivers not installed"

pkg-config --modversion libdpdk >/dev/null 2>&1 || err "libdpdk not found via pkg-config"

if ! lsmod | grep -q nvidia_peermem; then
    warn "nvidia_peermem not loaded - GPUDirect RDMA may not work"
    warn "Run: modprobe nvidia_peermem"
fi

if ! lsmod | grep -q gdrdrv; then
    warn "gdrdrv not loaded - GDRCopy may not work"
    warn "Run: modprobe gdrdrv"
fi

log "Prerequisites OK"
log "DPDK version: $(pkg-config --modversion libdpdk)"

if [[ "$DO_CLONE" == true ]]; then
    [[ -z "$REPO_URL" ]] && err "No repo URL specified. Use -r <url> or --no-clone to skip"

    if [[ -d "$BUILD_DIR" ]]; then
        warn "Directory $BUILD_DIR already exists, pulling latest..."
        git -C "$BUILD_DIR" fetch origin
        git -C "$BUILD_DIR" checkout "$BRANCH"
        git -C "$BUILD_DIR" pull origin "$BRANCH"
    else
        log "Cloning $REPO_URL (branch: $BRANCH) into $BUILD_DIR..."
        git clone --branch "$BRANCH" "$REPO_URL" "$BUILD_DIR"
    fi
else
    [[ -d "$BUILD_DIR" ]] || err "Directory $BUILD_DIR not found. Check -d or remove --no-clone"
    log "Skipping clone, using existing source in $BUILD_DIR"
fi

cd "$BUILD_DIR"


log "Configuring build with meson..."

if [[ -d build ]]; then
    warn "Existing build directory found, wiping..."
    rm -rf build
fi

meson setup build \
    --prefix="$DPDK_PREFIX" \
    -Dc_args="-I${CUDA_HOME}/include" \
    -Dc_link_args="-L${CUDA_HOME}/lib64"

log "Meson configuration complete"


log "Building with ninja ($(nproc) cores)..."
ninja -C build -j"$(nproc)"
log "Build complete"

if [[ "$DO_BIND" == true ]]; then
    [[ -z "$NIC_PCI" ]] && err "No NIC PCI address specified. Use -n <pci_addr> or --no-bind to skip"

    command -v dpdk-devbind.py >/dev/null 2>&1 || err "dpdk-devbind.py not found"

    log "Current device status:"
    dpdk-devbind.py --status-dev net


    if dpdk-devbind.py --status | grep "$NIC_PCI" | grep -q "vfio-pci"; then
        warn "$NIC_PCI already bound to vfio-pci, skipping"
    else

        IFACE=$(dpdk-devbind.py --status | grep "$NIC_PCI" | grep -oP 'if=\K\S+' || true)
        if [[ -n "$IFACE" ]]; then
            log "Bringing down interface $IFACE..."
            ip link set "$IFACE" down
        fi

        # log "Loading vfio-pci module..."
        # modprobe vfio-pci

        # log "Loading mlx5_core module..."
        modprobe mlx5_core

        log "Binding $NIC_PCI to vfio-pci..."
        dpdk-devbind.py --bind=mlx5_core "$NIC_PCI"
    fi

    log "Device status after binding:"
    dpdk-devbind.py --status-dev net
fi


echo ""
log "========================================"
log "VectorFlow-GX setup complete"
log "========================================"
log "Binary: $BUILD_DIR/build/vectflow-gx"
echo ""

# Build the run command
RUN_CMD="./build/vectflow-gx --iova-mode va"
[[ -n "$GPU_PCI" ]] && RUN_CMD+=" -a $GPU_PCI"
[[ -n "$NIC_PCI" ]] && RUN_CMD+=" -a $NIC_PCI"

log "Run with:"
echo "  cd $BUILD_DIR"
echo "  $RUN_CMD"
echo ""

if [[ -z "$GPU_PCI" ]]; then
    warn "No GPU PCI address specified (-g). Add -a <GPU_PCI> to your run command."
    warn "Find it with: lspci | grep -i nvidia"
fi
if [[ -z "$NIC_PCI" ]] && [[ "$DO_BIND" == false ]]; then
    warn "No NIC PCI address specified (-n). Add -a <NIC_PCI> to your run command."
    warn "Find it with: dpdk-devbind.py --status-dev net"
fi