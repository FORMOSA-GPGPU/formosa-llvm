{ stdenv
, lib
, cmake
, ninja
, python3
, pkg-config
, zlib
, stow
, ...
}@input:

let
  isBootstrap = !(input ? compiler-rt) || !(input ? newlib);
in
stdenv.mkDerivation {
  name = if isBootstrap then "formosa-llvm-bootstrap" else "formosa-llvm";
  src = ./.;

  configurePhase = ''
    cmake -B build -G Ninja -S llvm \
          -D CMAKE_INSTALL_PREFIX=$out \
          -D LLVM_ENABLE_PROJECTS="clang;lld;libclc" \
          -D CMAKE_BUILD_TYPE=Release \
          -D LLVM_DEFAULT_TARGET_TRIPLE=riscv64-unknown-elf \
          -D LLVM_TARGETS_TO_BUILD="RISCV" \
          -D LIBCLC_TARGETS_TO_BUILD="riscv64-unknown-elf" \
          -D BUILD_SHARED_LIBS=ON
  '';

  buildPhase = ''
    cmake --build build
  '';

  installPhase = ''
    cmake --build build --target install
    ${if !isBootstrap then ''
      CLANG_VERSION=$($out/bin/clang --version | grep -oP 'clang version \K[0-9]+')
      ln -s ${input.compiler-rt}/lib/linux $out/lib/clang/$CLANG_VERSION/lib
      cd ${input.newlib}/.. && stow ${builtins.baseNameOf input.newlib} -t $out
    '' else ""}
  '';

  nativeBuildInputs = [
    cmake
    ninja
    python3
    pkg-config
  ] ++ lib.optionals (!isBootstrap) [
    stow
  ];

  buildInputs = [
    zlib
  ] ++ lib.optionals (!isBootstrap) [
    input.compiler-rt
    input.newlib
  ];
}
