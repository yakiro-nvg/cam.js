const native = require('bindings')('cam-native')
import { Error } from './error'
import { readFileSync } from 'fs'

export interface Foreign
{
        (numUsings: number): void
}

export enum SlotType
{
        Unknown,
        Comp2,
        Comp4,
        Program,
        Display
}

export class Comp4
{
        scale: number
        isSigned: boolean
        value: BigInt

        constructor(isSigned: boolean, scale: number, value: BigInt)
        {
                this.isSigned = isSigned
                this.value    = value
                this.scale    = scale
        }
}

export interface CamNative
{
        addChunkBuffer(buf: Buffer): Error
        addForeign(module: string, program: string, foreign: Foreign): void
        link(): Error
        ensureSlots(numSlots: number): void
        numSlots(): number
        slotType(slot: number): SlotType
        setSlotComp2(slot: number, value: number): void
        setSlotComp4(slot: number, value: Comp4): void
        setSlotProgram(slot: number, module: string, program: string): void
        setSlotDisplay(slot: number, value?: string): void
        getSlotComp2(slot: number): number
        getSlotComp4(slot: number): Comp4
        getSlotDisplay(slot: number): string
        slotCopy(dstSlot: number, srcSlot: number): void
        call(numUsings: number, numReturnings: number): void
        protectedCall(numUsings: number, numReturnings: number): void
}

export var CamNative: {
        new(): CamNative
} = native.CamNative

export class Cam extends CamNative
{
        constructor()
        {
                super()
        }

        addChunk(path: string): Error
        {
                return this.addChunkBuffer(readFileSync(path))
        }
}
