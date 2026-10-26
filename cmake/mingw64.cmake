# -----------------------------------------------------------------------------
# Windows 64-bit, cross-compiled with MinGW-w64 (use with mingw64-toolchain.cmake).
#   cmake ../cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw64-toolchain.cmake -DTARGET=mingw64.cmake
#         -DSDL2_ROOT=<SDL2-devel-mingw>/x86_64-w64-mingw32 -DBOOST_INC=<dir containing boost/>
# Static libgcc/libstdc++ so the .exe only needs SDL2.dll next to it.
# -----------------------------------------------------------------------------
set(boost_dir ${BOOST_INC})
set(sdl2_dir ${SDL2_ROOT}/lib/cmake/SDL2)
set(OPENGL 1)
set(platform_link_libs opengl32 glu32 dxguid dinput8 -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
