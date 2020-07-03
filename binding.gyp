{
        "targets": [
                {
                        "target_name": "cam-native",
                        "sources": [
                                "<!@(node -p \"require('fs').readdirSync('vendor/cam/src/').filter(f => f.endsWith('.c')).map(f => 'vendor/cam/src/' + f).join(' ')\")",
                                "src/cam_native.cc"
                        ],
                        "include_dirs": [
                                "<!(node -e \"require('nan')\")",
                                "vendor/cam/include"
                        ],
                        "conditions": [
                                ["OS=='win'", {
                                        'defines': [
                                                "CAM_TASK_FIBER"
                                        ]
                                }, {
                                        'defines': [
                                                "CAM_TASK_GCC_ASM"
                                        ]
                                }],
                        ]
                }
        ]
}
