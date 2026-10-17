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

        # Hardening: disable only "fortify" (keep everything else that nixpkgs enables)
        hardeningDisableFortify = "stackprotector pie pic strictoverflow format relro bindnow";

        # Extra CMake flags for this environment (Clang + LTO)
        extraFlags = builtins.concatStringsSep " " [
          "-DCMAKE_CXX_COMPILER_AR=${llvmTools}/bin/llvm-ar"
          "-DCMAKE_CXX_COMPILER_RANLIB=${llvmTools}/bin/llvm-ranlib"
          "-DCMAKE_LINKER_TYPE=LLD"
        ];

        # Common packages for both shells
        commonPackages = [
          pkgs.git
          cmake
          pkgs.ninja
          pkgs.perf
        ];

                # --- Linux (native) shell ---
        linuxShell = pkgs.mkShell {
          nativeBuildInputs = commonPackages ++ [
            gcc
            clang
            lld
            glibcDev
            llvmTools
          ];

          env = {
            CXXFLAGS = flags;
            CFLAGS = flags;
            NIX_LDFLAGS = with builtins; concatStringsSep " " [
              "-L${gccUnwrapped}/lib"
              "-L${gccUnwrapped}/lib64"
            ];
            CXX_MODULES_JSON = "${gccUnwrapped}/lib/libstdc++.modules.json";
            USE_LLVM_LTO = "1";
            NIX_HARDENING_ENABLE = hardeningDisableFortify;
          };

          shellHook = ''
            export CC="${clang}/bin/clang"
            export CXX="${clang}/bin/clang++"
            echo "C compiler:   $CC   ($( $CC   --version | head -n1 ))"
            echo "C++ compiler: $CXX ($( $CXX --version | head -n1 ))"
            settings --toolchain "" --cmake-extra-flags "${extraFlags}"
          '';
        };

        # --- Windows (cross-compilation) shell ---
        mingwPkgs = pkgs.pkgsCross.mingwW64;
        windowsShell = mingwPkgs.mkShell {
          nativeBuildInputs = commonPackages ++ [
            mingwPkgs.stdenv.cc
            pkgs.wine64Packages.stable
          ];

          # mcfgthreads is a Windows‑target library – it belongs in buildInputs.
          buildInputs = [
            mingwPkgs.windows.mcfgthreads
          ];

          shellHook = ''
            # Host flags from `linuxShell` must NOT leak into a cross build.
            unset CXXFLAGS CFLAGS CPPFLAGS LDFLAGS
            unset NIX_CFLAGS_COMPILE NIX_CXXSTDLIB_COMPILE NIX_LDFLAGS
            unset CXX_MODULES_JSON
            export NIX_LDFLAGS="-L${mingwPkgs.windows.mcfgthreads}/lib $NIX_LDFLAGS"

            export CC="${mingwPkgs.stdenv.cc}/bin/x86_64-w64-mingw32-gcc"
            export CXX="${mingwPkgs.stdenv.cc}/bin/x86_64-w64-mingw32-g++"

            export CXX_MODULES_JSON="$($CXX -print-file-name=libstdc++.modules.json)"

            echo "Windows cross-compiler: $CXX"
            $CXX --version | head -n1

            settings --toolchain windows-x86_64 --cmake-extra-flags "" --sanitizers ""
          '';
        };
      in
      {
        devShells = {
          default = linuxShell;
          windows = windowsShell;
        };
      }
    );
}
