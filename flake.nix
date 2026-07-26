{
  description = "Dev shell with GCC 16, Clang 22, CMake 4.3.3";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Override CMake – same version, same source as your devenv
        cmake = pkgs.cmake.overrideAttrs (old: rec {
          version = "4.3.3";
          src = pkgs.fetchurl {
            url = "https://github.com/Kitware/CMake/releases/download/v${version}/cmake-${version}.tar.gz";
            hash = "sha256-y6S7ekTt8od7tvBZkyiWODur5DWzqMO130i0qkHJu4U=";
          };
          patches = [ ];
          meta = old.meta // {
            priority = 0;
          };
        });

        # Compiler toolchains
        gcc = pkgs.gcc16;
        gccUnwrapped = gcc.cc;

        llvm = pkgs.llvmPackages_22;
        clang = llvm.libstdcxxClang;
        llvmTools = llvm.llvm;
        lld = llvm.lld;

        glibcDev = pkgs.glibc.dev;

        # Include flags from your devenv
        flags =
          with builtins;
          concatStringsSep " " [
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}"
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}/x86_64-unknown-linux-gnu"
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}/backward"
            "-isystem ${glibcDev}/include"
          ];

        settingsScript = pkgs.writeShellApplication {
          name = "settings";
          text = ''
            CONFIG_FILE="''${FSC_CONFIG_FILE:-$PROJECT_ROOT/.fsc_config}"

            if [ -z "$CONFIG_FILE" ]; then
              echo "ERROR: FSC_CONFIG_FILE or PROJECT_ROOT not set." >&2
              exit 1
            fi

            # If no arguments, print current settings
            if [ $# -eq 0 ]; then
              if [ -f "$CONFIG_FILE" ]; then
                # shellcheck disable=SC1090
                source "$CONFIG_FILE"
                echo "Current settings (from $CONFIG_FILE):"
                echo "  BUILD_TYPE               = $BUILD_TYPE"
                echo "  ENABLE_TESTS             = $ENABLE_TESTS"
                echo "  SANITIZERS               = $SANITIZERS"
                echo "  ENABLE_LTO               = $ENABLE_LTO"
                echo "  BUILD_SHARED_LIBS        = $BUILD_SHARED_LIBS"
                echo "  WARNINGS_LEVEL           = $WARNINGS_LEVEL"
                echo "  TREAT_WARNINGS_AS_ERRORS = $TREAT_WARNINGS_AS_ERRORS"
              else
                echo "No config file found at $CONFIG_FILE"
              fi
              exit 0
            fi

            # Parse new values
            declare -A new
            while [[ $# -gt 0 ]]; do
              case "$1" in
                --build-type)          new[BUILD_TYPE]="$2"; shift 2 ;;
                --tests)               new[ENABLE_TESTS]="$2"; shift 2 ;;
                --sanitizers)          new[SANITIZERS]="$2"; shift 2 ;;
                --lto)                 new[ENABLE_LTO]="$2"; shift 2 ;;
                --build-shared-libs)   new[BUILD_SHARED_LIBS]="$2"; shift 2 ;;
                --warnings-level)      new[WARNINGS_LEVEL]="$2"; shift 2 ;;
                --warnings-as-errors)  new[TREAT_WARNINGS_AS_ERRORS]="$2"; shift 2 ;;
                *)
                  echo "Unknown option: $1" >&2
                  echo "Usage: settings [--build-type Release|Debug] [--tests ON|OFF] ..." >&2
                  exit 1
                  ;;
              esac
            done

            # Load existing config (if any) to preserve other keys
            if [ -f "$CONFIG_FILE" ]; then
              # shellcheck disable=SC1090
              source "$CONFIG_FILE"
            fi

            # Override with newly supplied values
            for key in "''${!new[@]}"; do
              declare "$key=''${new[$key]}"
            done

            # Write the config file
            true > "$CONFIG_FILE"
            for var in BUILD_TYPE ENABLE_TESTS SANITIZERS ENABLE_LTO BUILD_SHARED_LIBS \
                       WARNINGS_LEVEL TREAT_WARNINGS_AS_ERRORS API APP_NAME \
                       APP_VERSION_MAJOR APP_VERSION_MINOR APP_VERSION_PATCH; do
              echo "$var=''${!var}" >> "$CONFIG_FILE"
            done

            echo "Settings written to $CONFIG_FILE"
          '';
        };

        cleanScript = pkgs.writeShellApplication {
          name = "clean";
          text = ''
            cd "$PROJECT_ROOT"
            rm -rf build/
          '';
        };

        buildScript = pkgs.writeShellApplication {
          name = "build";
          text = ''
            if [ -f "$PROJECT_ROOT/.fsc_config" ]; then
              # shellcheck disable=SC1091
              source "$PROJECT_ROOT/.fsc_config"
            else
              echo "Warning: .fsc_config not found; using environment defaults" >&2
            fi

            cd "$PROJECT_ROOT"
            cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
              -DENABLE_TESTS="$ENABLE_TESTS" \
              -DSANITIZERS="$SANITIZERS" \
              -DENABLE_LTO="$ENABLE_LTO" \
              -DBUILD_SHARED_LIBS="$BUILD_SHARED_LIBS" \
              -DWARNINGS_LEVEL="$WARNINGS_LEVEL" \
              -DTREAT_WARNINGS_AS_ERRORS="$TREAT_WARNINGS_AS_ERRORS" \
              -DCMAKE_CXX_COMPILER_AR="${llvmTools}/bin/llvm-ar" \
              -DCMAKE_CXX_COMPILER_RANLIB="${llvmTools}/bin/llvm-ranlib" \
              -DCMAKE_LINKER_TYPE=LLD \
              -B build -G Ninja
          '';
        };

        compileScript = pkgs.writeShellApplication {
          name = "compile";
          text = ''
            if [ -f "$PROJECT_ROOT/.fsc_config" ]; then
              # shellcheck disable=SC1091
              source "$PROJECT_ROOT/.fsc_config"
            fi

            CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

            while [[ $# -gt 0 ]]; do
              case "$1" in
                --cores) CORES="$2"; shift 2 ;;
                *)
                  echo "Unknown option: $1" >&2;
                  echo "Usage compile [--cores 5]"
                  return 1
                  ;;
              esac
            done

            echo "CORES = $CORES"
            cd "$PROJECT_ROOT/build"
            ninja -j "$CORES"
          '';
        };

        testScript = pkgs.writeShellApplication {
          name = "tests";
          text = ''
            cd "$PROJECT_ROOT/build"
            ninja test
          '';
        };

        # Hardening: disable only "fortify" (keep everything else that nixpkgs enables)
        hardeningDisableFortify = "stackprotector pie pic strictoverflow format relro bindnow";
      in
      {
        devShells.default = pkgs.mkShell {
          # Packages available in the shell
          nativeBuildInputs = [
            pkgs.git
            gcc
            clang
            lld
            cmake
            pkgs.ninja
            glibcDev

            settingsScript
            cleanScript
            buildScript
            compileScript
            testScript
          ];

          env = {
            CXXFLAGS = flags;
            CFLAGS = flags;

            NIX_LDFLAGS =
              with builtins;
              concatStringsSep " " [
                "-L${gccUnwrapped}/lib"
                "-L${gccUnwrapped}/lib64"
              ];

            CXX_MODULES_JSON = "${gccUnwrapped}/lib/libstdc++.modules.json";

            BUILD_TYPE = "Debug";
            ENABLE_TESTS = "ON";
            SANITIZERS = "address,undefined";
            ENABLE_LTO = "ON";
            BUILD_SHARED_LIBS = "ON";
            WARNINGS_LEVEL = 2;
            TREAT_WARNINGS_AS_ERRORS = "OFF";

            # Disable fortify hardening only
            NIX_HARDENING_ENABLE = hardeningDisableFortify;
          };

          # Shell hook: runs after entering the shell (enterShell equivalent)
          shellHook = ''
            export PROJECT_ROOT="$PWD"
            export CC="${clang}/bin/clang"
            export CXX="${clang}/bin/clang++"

            export FSC_CONFIG_FILE="$PROJECT_ROOT/.fsc_config"

            if [ ! -f "$FSC_CONFIG_FILE" ]; then
              cat > "$FSC_CONFIG_FILE" <<'EOF'
            BUILD_TYPE="Debug"
            ENABLE_TESTS="ON"
            SANITIZERS="address,undefined"
            ENABLE_LTO="ON"
            BUILD_SHARED_LIBS="ON"
            WARNINGS_LEVEL=2
            TREAT_WARNINGS_AS_ERRORS="OFF"
            EOF
              echo "Created default config at $FSC_CONFIG_FILE"
            fi

            load_settings() {
              # shellcheck disable=SC1090
              source "$FSC_CONFIG_FILE"
              export BUILD_TYPE ENABLE_TESTS SANITIZERS ENABLE_LTO BUILD_SHARED_LIBS \
                     WARNINGS_LEVEL TREAT_WARNINGS_AS_ERRORS API APP_NAME \
                     APP_VERSION_MAJOR APP_VERSION_MINOR APP_VERSION_PATCH
            }

            load_settings

            echo "C compiler:   $CC   ($( $CC   --version | head -n1 ))"
            echo "C++ compiler: $CXX ($( $CXX --version | head -n1 ))"
          '';
        };
      }
    );
}
