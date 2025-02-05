const native = require('bindings')('cam-native')
import { v4 as uuidv4 } from 'uuid'
import { Comp4 } from './cam'

export enum Opcode
{
        Nop,
        Pop,
        Push,
        NumUsings,
        Replace,
        RelativeReplace,
        Load,
        Store,
        Import,
        ChunkValue,
        Jump,
        JumpIfNot,
        Call,
        Return,
        BinaryOp,
        Display
}

export interface AssemblerNative
{
        serialize(path: string): void
        wfieldComp2(value: number): number
        wfieldComp4(comp4: Comp4): number
        wfieldDisplay(value?: string): number
        import(module: string, program: string): number
        emitA(opcode: Opcode): number
        emitB(opcode: Opcode, b0: number): number
        emitC(opcode: Opcode, c0: number, c1: number): number
        prototypePush(name?: string): number
        prototypePop(): void
}

export var AssemblerNative: {
        new(module: string, uuid: Buffer): AssemblerNative
} = native.AssemblerNative

export class Assembler extends AssemblerNative
{
        constructor(module: string)
        {
                const uuid = Buffer.alloc(16)
                uuidv4(null, uuid, 0)
                super(module, uuid)
        }
}
