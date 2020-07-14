{
        "targets": [
                {
                        "target_name": "cam-native",
                        "sources": [
                                "<!@(node -p \"require('fs').readdirSync('vendor/cam/src/').filter(f => f.endsWith('.c')).map(f => 'vendor/cam/src/' + f).join(' ')\")",
                                "src/cam_native.cc",
                                "src/assembler_native.cc",
                                "src/init_modules.cc"
                        ],
                        "include_dirs": [
                                "<!(node -e \"require('nan')\")",
                                "vendor/cam/include"
                        ]
                }
        ]
}
