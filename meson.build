project('ResourceWrapperFS', 'cpp', version: '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++20'])

deps = [
  dependency('fuse3'),
]

executable('rwfs', files('src/main.cpp'),
            dependencies : deps,
            install : true)
