{ stdenv
, cmake
, ninja
, python3
, pkg-config
, zlib
, formosa-llvm
}:

stdenv.mkDerivation {
  name = "compiler-rt";
  src = ./.;

  configurePhase = ''
    cmake -B build -G Ninja -S compiler-rt \
    	    -D CMAKE_INSTALL_PREFIX=$out \
    	    -D CMAKE_C_COMPILER_TARGET="riscv64-unknown-elf" \
    	    -D CMAKE_ASM_COMPILER_TARGET="riscv64-unknown-elf" \
    	    -D COMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    	    -D COMPILER_RT_BAREMETAL_BUILD=ON \
    	    -D COMPILER_RT_BUILD_BUILTINS=ON \
    	    -D COMPILER_RT_BUILD_LIBFUZZER=OFF \
    	    -D COMPILER_RT_BUILD_MEMPROF=OFF \
    	    -D COMPILER_RT_BUILD_PROFILE=OFF \
    	    -D COMPILER_RT_BUILD_SANITIZERS=OFF \
    	    -D COMPILER_RT_BUILD_XRAY=OFF \
    	    -D CMAKE_C_COMPILER_WORKS=1 \
    	    -D CMAKE_CXX_COMPILER_WORKS=1 \
    	    -D CMAKE_SIZEOF_VOID_P=4 \
    	    -D CMAKE_C_COMPILER="${formosa-llvm}/bin/clang" \
    	    -D CMAKE_C_FLAGS="-march="rv64im_zicsr_zicond" -mabi=lp64 -mno-relax -mcmodel=medany" \
    	    -D CMAKE_ASM_FLAGS="-march="rv64im_zicsr_zicond" -mabi=lp64 -mno-relax -mcmodel=medany" \
    	    -D CMAKE_AR=${formosa-llvm}/bin/llvm-ar \
    	    -D CMAKE_NM=${formosa-llvm}/bin/llvm-nm \
    	    -D CMAKE_RANLIB=${formosa-llvm}/bin/llvm-ranlib \
    	    -D LLVM_CONFIG_PATH=${formosa-llvm}/bin/llvm-config
  '';

  buildPhase = ''
    cmake --build build
  '';

  installPhase = ''
    cmake --build build --target install
  '';

  nativeBuildInputs = [
    cmake
    ninja
    python3
    pkg-config
  ];

  buildInputs = [
    zlib
  ];
}
