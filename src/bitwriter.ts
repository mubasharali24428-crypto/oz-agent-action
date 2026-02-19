/**
 * BitWriter provides bit-level write operations, packing individual bits
 * into a Uint8Array buffer. Bits are written MSB-first within each byte.
 */
export class BitWriter {
  private buffer: number[] = []
  private currentByte: number = 0
  private bitCount: number = 0

  /**
   * Write a single bit (0 or 1) to the buffer.
   */
  writeBit(bit: number): void {
    if (bit !== 0 && bit !== 1) {
      throw new Error(`Invalid bit value: ${bit}`)
    }
    this.currentByte = (this.currentByte << 1) | bit
    this.bitCount++
    if (this.bitCount === 8) {
      this.buffer.push(this.currentByte)
      this.currentByte = 0
      this.bitCount = 0
    }
  }

  /**
   * Write a string of '0' and '1' characters as individual bits.
   */
  writeBits(bits: string): void {
    for (const ch of bits) {
      this.writeBit(ch === '1' ? 1 : 0)
    }
  }

  /**
   * Flush remaining bits and return the packed buffer.
   * Any trailing bits in the last byte are left-aligned (padded with zeros on the right).
   * Returns a tuple of [data, paddingBits] where paddingBits is the number of
   * zero-padding bits added to the final byte (0–7).
   */
  flush(): [Uint8Array, number] {
    let padding = 0
    if (this.bitCount > 0) {
      padding = 8 - this.bitCount
      this.currentByte <<= padding
      this.buffer.push(this.currentByte)
      this.currentByte = 0
      this.bitCount = 0
    }
    return [new Uint8Array(this.buffer), padding]
  }
}

/**
 * BitReader provides bit-level read operations, unpacking individual bits
 * from a Uint8Array buffer. Bits are read MSB-first within each byte.
 */
export class BitReader {
  private data: Uint8Array
  private byteIndex: number = 0
  private bitIndex: number = 0
  private totalBits: number

  /**
   * @param data - The packed byte buffer to read from.
   * @param paddingBits - Number of padding bits in the last byte to ignore (0–7).
   */
  constructor(data: Uint8Array, paddingBits: number = 0) {
    this.data = data
    this.totalBits = data.length * 8 - paddingBits
  }

  /**
   * Returns true if there are more bits available to read.
   */
  hasMore(): boolean {
    return this.byteIndex * 8 + this.bitIndex < this.totalBits
  }

  /**
   * Read a single bit (0 or 1) from the buffer.
   * Throws if no more bits are available.
   */
  readBit(): number {
    if (!this.hasMore()) {
      throw new Error('No more bits to read')
    }
    const bit = (this.data[this.byteIndex] >> (7 - this.bitIndex)) & 1
    this.bitIndex++
    if (this.bitIndex === 8) {
      this.bitIndex = 0
      this.byteIndex++
    }
    return bit
  }
}
