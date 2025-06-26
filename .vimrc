set expandtab
set shiftwidth=2
set tabstop=2

let g:marching_include_paths = [
\ '/usr/include/SDL2',
\]

let g:marching#clang_command#options = {
\ "cpp" : "-std=c++14"
\}
