var fs = require('fs')
var native = require('bindings')('cam-native')
var cam = new native.CamNative()

var chunk_buf = fs.readFileSync("C:/Users/Admin/Documents/GitHub/cam/build/tools/brutus/Debug/test.cam")

cam.addForeign("B-OPS", "SUB", aid => {
        console.log(cam.getSlotComp4(aid, 0))
        console.log(cam.getSlotComp4(aid, 1))
        cam.setSlotComp4(aid, 0, { value: BigInt(-99), precision: 3, scale: 2 })
})

cam.addChunkBuffer(chunk_buf)
cam.link()

cam.dispatch(aid => {
        cam.ensureSlots(aid, 1)
        cam.setSlotProgram(aid, 0, "TEST", "F")
        cam.call(aid, 0, 0)
})

cam.runOnce()
