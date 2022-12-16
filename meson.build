project('ResourceWrapperFS', 'cpp', version: '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++20'])

subdir('src/drivers/jxl')
subdir('src/drivers/flac')

driver_deps = jxl_deps + flac_deps

executable('rwfs', files('src/main.cpp'),
            dependencies : [dependency('fuse3')] + driver_deps,
            install : true)

executable('codec-test', files('src/codec.cpp'),
            dependencies : driver_deps,
            install : true)
