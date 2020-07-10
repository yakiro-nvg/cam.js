var fs = require('fs')
var native = require('bindings')('cam-native')
var cam = new native.CamNative()

var chunk_buf = fs.readFileSync("C:/Users/Admin/Documents/GitHub/cam/build/tools/brutus/Debug/test.cam")

cam.addForeign("B-OPS", "SUB", () => {
        console.log(cam.getSlotComp4(0))
        console.log(cam.getSlotComp4(1))
        cam.setSlotComp2(1, 8.13)
        console.log(cam.getSlotComp2(1))
        cam.setSlotComp4(0, { value: BigInt(-99), precision: 3, scale: 2 })
        cam.setSlotDisplay(1, "john")
        console.log("slot_1: " + cam.getSlotDisplay(1))
        console.log("slot_1_type: " + cam.slotType(0))
        console.log("slot_2_type: " + cam.slotType(1))
})

cam.addChunkBuffer(chunk_buf)
cam.link()

cam.ensureSlots(1)
cam.setSlotProgram(0, "TEST", "F")
cam.protectedCall(0, 0)
