#!/usr/bin/env bash
set -euo pipefail

#===============================================================================
# Minecraft 1.21.11 Fabric Server Setup — Linux (ARM64 / x86_64)
#===============================================================================
SERVER_DIR="$(cd "$(dirname "$0")" && pwd)/mcserver"
DATA_DIR="$SERVER_DIR/data"
MODS_DIR="$DATA_DIR/mods"
JRE_DIR="$SERVER_DIR/jre"

JAVA_EXE="java"
SERVER_PORT=25557

mkdir -p "$DATA_DIR" "$MODS_DIR"

echo "==================================================="
echo "  Minecraft 1.21.11 Fabric Server Setup"
echo "==================================================="
echo "Server dir: $SERVER_DIR"

#=============================================================================
# Step 1: Java 21
#=============================================================================
ARCH="$(uname -m)"
case "$ARCH" in
    aarch64) ARCH_NAME="aarch64" ;;
    x86_64)  ARCH_NAME="x64" ;;
    *)       echo "Unsupported arch: $ARCH"; exit 1 ;;
esac

if [ -x "$JRE_DIR/bin/java" ]; then
    JAVA_EXE="$JRE_DIR/bin/java"
    echo "[INFO] Using existing portable Java: $JAVA_EXE"
else
    if command -v java &>/dev/null; then
        JAVA_VER=$(java -version 2>&1 | head -1 | sed 's/.*version "\([0-9]*\).*/\1/')
        if [ "$JAVA_VER" -ge 21 ]; then
            echo "[INFO] System Java $JAVA_VER is sufficient"
        else
            echo "[WARN] System Java is version $JAVA_VER; need 21+"
            echo "[INFO] Downloading portable JDK 21..."
            mkdir -p "$JRE_DIR"
            JDK_URL="https://api.adoptium.net/v3/binary/latest/21/ga/linux/${ARCH_NAME}/jdk/hotspot/normal/eclipse"
            curl -fsSL "$JDK_URL" -o /tmp/jdk21.tar.gz
            tar -xzf /tmp/jdk21.tar.gz -C "$JRE_DIR" --strip-components=1
            JAVA_EXE="$JRE_DIR/bin/java"
            echo "[SUCCESS] Portable JDK 21 ready"
        fi
    else
        echo "[INFO] Downloading portable JDK 21..."
        mkdir -p "$JRE_DIR"
        JDK_URL="https://api.adoptium.net/v3/binary/latest/21/ga/linux/${ARCH_NAME}/jdk/hotspot/normal/eclipse"
        curl -fsSL "$JDK_URL" -o /tmp/jdk21.tar.gz
        tar -xzf /tmp/jdk21.tar.gz -C "$JRE_DIR" --strip-components=1
        JAVA_EXE="$JRE_DIR/bin/java"
        echo "[SUCCESS] Portable JDK 21 ready"
    fi
fi

echo "[INFO] Java: $($JAVA_EXE -version 2>&1 | head -1)"

#=============================================================================
# Step 2: Fabric server jar
#=============================================================================
FABRIC_JAR="$DATA_DIR/fabric-server-launch.jar"
if [ -f "$FABRIC_JAR" ]; then
    echo "[INFO] Fabric server jar exists: $FABRIC_JAR"
else
    echo "[INFO] Downloading Fabric installer..."
    INSTALLER="$SERVER_DIR/fabric-installer.jar"
    curl -fsSL "https://maven.fabricmc.net/net/fabricmc/fabric-installer/1.1.0/fabric-installer-1.1.0.jar" -o "$INSTALLER"
    echo "[INFO] Running Fabric installer for 1.21.11..."
    cd "$DATA_DIR"
    "$JAVA_EXE" -jar "$INSTALLER" server -mcversion 1.21.11 -downloadMinecraft
    cd "$SERVER_DIR"
    rm -f "$INSTALLER"
    # The installer creates fabric-server-launch.jar in the working dir
    if [ -f "$DATA_DIR/fabric-server-launch.jar" ]; then
        echo "[SUCCESS] Fabric server installed"
    else
        # Try finding it
        FOUND_JAR=$(find "$DATA_DIR" -name "fabric-server*.jar" | head -1)
        if [ -n "$FOUND_JAR" ]; then
            FABRIC_JAR="$FOUND_JAR"
            echo "[SUCCESS] Found fabric jar: $FABRIC_JAR"
        else
            echo "[ERROR] Fabric installation failed"
            exit 1
        fi
    fi
fi

#=============================================================================
# Step 3: Server config
#=============================================================================
cat > "$DATA_DIR/eula.txt" << 'EOF'
eula=true
EOF

cat > "$DATA_DIR/server.properties" << EOF
online-mode=false
server-port=$SERVER_PORT
motd=McChunkGen GPU-Accelerated Server
difficulty=easy
max-players=20
view-distance=10
simulation-distance=6
spawn-protection=0
sync-chunk-writes=false
allow-flight=true
enable-query=true
query-port=9900
use-native-transport=true
level-type=default
generator-settings={}
EOF

echo "[INFO] Server configured (port $SERVER_PORT)"

#=============================================================================
# Step 4: Download mods from Modrinth
#=============================================================================
declare -A MOD_PROJECTS=(
    ["fabric-api"]="release"
    ["fabric-language-kotlin"]="release"
    ["chunky"]="release"
    ["lithium"]="release"
    ["ferrite-core"]="release"
    ["modernfix"]="release"
    ["servercore"]="release"
    ["krypton"]="release"
    ["c2me-fabric"]="release"
    ["noisium"]="release"
)

echo "[INFO] Downloading mods from Modrinth..."
for PROJECT in "${!MOD_PROJECTS[@]}"; do
    CHANNEL="${MOD_PROJECTS[$PROJECT]}"
    # Check if already installed
    EXISTING=$(find "$MODS_DIR" -maxdepth 1 -name "*${PROJECT}*.jar" 2>/dev/null | head -1)
    if [ -n "$EXISTING" ]; then
        echo "[SKIP] $PROJECT already installed ($(basename "$EXISTING"))"
        continue
    fi

    echo "[DOWNLOAD] $PROJECT..."

    # Try with exact game version first
    API_URL="https://api.modrinth.com/v2/project/${PROJECT}/version?loaders=[%22fabric%22]&game_versions=[%221.21.11%22]"
    VERSIONS=$(curl -fsSL -H "User-Agent: mc-server-setup" "$API_URL" 2>/dev/null || echo "[]")

    if [ "$VERSIONS" = "[]" ]; then
        # Fall back to any fabric version
        API_URL="https://api.modrinth.com/v2/project/${PROJECT}/version?loaders=[%22fabric%22]"
        VERSIONS=$(curl -fsSL -H "User-Agent: mc-server-setup" "$API_URL" 2>/dev/null || echo "[]")
    fi

    if [ "$VERSIONS" = "[]" ]; then
        echo "[WARN] No versions found for $PROJECT"
        continue
    fi

    # Pick version based on channel preference
    SELECTED=$(echo "$VERSIONS" | "$JAVA_EXE" -cp /dev/null - 2>/dev/null || python3 -c "
import json, sys
data = json.load(sys.stdin)
# Prefer release, then beta, then alpha
for t in ['release', 'beta', 'alpha']:
    for v in data:
        if v['version_type'] == t:
            print(json.dumps(v))
            sys.exit(0)
print(json.dumps(data[0]))
" <<< "$VERSIONS" 2>/dev/null || echo "$VERSIONS" | python3 -c "
import json, sys
data = json.load(sys.stdin)
print(json.dumps(data[0]))
")

    # Extract the primary file
    DL_URL=$(echo "$SELECTED" | python3 -c "
import json, sys
d = json.load(sys.stdin)
files = d.get('files', [])
primary = [f for f in files if f.get('primary')]
if primary:
    print(primary[0]['url'])
elif files:
    print(files[0]['url'])
" 2>/dev/null)

    FILENAME=$(echo "$SELECTED" | python3 -c "
import json, sys
d = json.load(sys.stdin)
files = d.get('files', [])
primary = [f for f in files if f.get('primary')]
if primary:
    print(primary[0]['filename'])
elif files:
    print(files[0]['filename'])
" 2>/dev/null)

    if [ -n "$DL_URL" ] && [ -n "$FILENAME" ]; then
        echo "[DOWNLOAD] Saving $FILENAME..."
        curl -fsSL -o "$MODS_DIR/$FILENAME" "$DL_URL"
        echo "[OK] $FILENAME"
    else
        echo "[WARN] Could not resolve download for $PROJECT"
        echo "  Response snippet: $(echo "$VERSIONS" | head -c 200)"
    fi
done

#=============================================================================
# Step 5: Build + deploy McChunkGen native library
#=============================================================================
echo "[INFO] Building McChunkGen native library..."
cd "$SERVER_DIR/.."
make -C mod/mcchunkgen/bridge CXX="$JAVA_EXE" clean 2>/dev/null || true
# Build using the project Makefile with explicit CXX for the bridge
if command -v g++ &>/dev/null; then
    make -C mod/mcchunkgen/bridge install 2>&1 || {
        echo "[WARN] Bridge build failed, attempting direct build"
        cd mod/mcchunkgen/bridge
        g++ -O3 -march=native -ffast-math -pthread -std=c++17 -fPIC -x c++ -c ../../../generator.cu -o generator_bridge.o
        g++ -O3 -march=native -ffast-math -pthread -std=c++17 -fPIC -c jni_bridge.c -o jni_bridge.o
        g++ -shared generator_bridge.o jni_bridge.o -pthread -o libmcchunkgen.so
        cp libmcchunkgen.so ../src/main/resources/
        cd "$SERVER_DIR"
    }
else
    echo "[WARN] No g++ found, skipping native library build"
    echo "[WARN] McChunkGen mod will fall back to vanilla generation"
fi

#=============================================================================
# Step 6: Build McChunkGen Fabric mod
#=============================================================================
echo "[INFO] Building McChunkGen Fabric mod..."
cd "$SERVER_DIR/.."
if command -v gradle &>/dev/null; then
    cd mod/mcchunkgen
    gradle build 2>&1 || echo "[WARN] Gradle build failed — mod JAR may not be ready"
    cd "$SERVER_DIR"
else
    echo "[SKIP] No Gradle available — cannot build mod JAR"
    echo "[INFO] You can build the mod on a machine with JDK 21 + Fabric Loom"
fi

# Copy mod JAR if it exists
MOD_JAR=$(find "$SERVER_DIR/../mod/mcchunkgen/build/libs" -name "*.jar" 2>/dev/null | head -1)
if [ -n "$MOD_JAR" ]; then
    cp "$MOD_JAR" "$MODS_DIR/"
    echo "[OK] McChunkGen mod deployed to mods/"
fi

echo ""
echo "==================================================="
echo "  Setup complete!"
echo "  Server:  $DATA_DIR"
echo "  Java:    $JAVA_EXE"
echo "  JAR:     $FABRIC_JAR"
echo "  Port:    $SERVER_PORT"
echo "==================================================="
echo ""
echo "To launch:"
echo "  $JAVA_EXE -Xms4G -Xmx4G \\"
echo "    -XX:+UseZGC -XX:+AlwaysPreTouch \\"
echo "    -XX:+PerfDisableSharedMem -XX:+DisableExplicitGC \\"
echo "    -XX:AllocatePrefetchStyle=1 -XX:ReservedCodeCacheSize=512M \\"
echo "    -XX:NonNMethodCodeHeapSize=12M -XX:ProfiledCodeHeapSize=250M \\"
echo "    -XX:NonProfiledCodeHeapSize=250M \\"
echo "    -Dterminal.jline=false -Dterminal.ansi=true \\"
echo "    -jar $FABRIC_JAR nogui"
echo ""