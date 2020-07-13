var fs = require('fs')
var native = require('bindings')('cam-native')
var cam = new native.CamNative()

cam.addForeign("B-OPS", "SUB", (pattern, num_usings) => {
        console.log("pattern: " + pattern + ", num_usings: " + num_usings)
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

cam.link()

cam.ensureSlots(3)
cam.setSlotProgram(0, "B-OPS", "SUB")
cam.setSlotComp4(1, { value: BigInt(314), precision: 3, scale: 2 })
cam.setSlotComp4(2, { value: BigInt(218), precision: 3, scale: 2 })
cam.protectedCall(2, 0)
