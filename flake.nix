{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        formosa-llvm = pkgs.callPackage ./package.nix { };
      in
      {
        packages = {
          inherit formosa-llvm;
          default = formosa-llvm;
        };

        devShells.default = pkgs.mkShell.override
        {
          stdenv = pkgs.gccStdenv;
        }
        {
          name = "formosa-llvm";
          cmakeFlags = [
            "-DLLVM_ENABLE_PROJECTS='clang;lld;libclc'"
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_C_COMPILER=gcc"
            "-DCMAKE_CXX_COMPILER=g++"
            "-DLLVM_DEFAULT_TARGET_TRIPLE=riscv64-unknown-elf"
            "-DLLVM_TARGETS_TO_BUILD='RISCV;X86'"
            "-DLLVM_USE_LINKER=lld"
            "-DLIBCLC_TARGETS_TO_BUILD='riscv64-unknown-elf'"
            "-DLLVM_BUILD_LLVM_DYLIB=ON"
          ];
          
          packages = with pkgs; [
            cmake
            ninja
            python3
            pkg-config
            zlib
          ];
        };
      }
    );
}
