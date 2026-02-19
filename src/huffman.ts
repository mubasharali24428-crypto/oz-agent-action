import { BitWriter, BitReader } from './bitwriter.js'

/**
 * A node in the Huffman tree. Leaf nodes hold a symbol (byte value);
 * internal nodes have left and right children.
 */
export class HuffmanNode {
  symbol: number | null
  frequency: number
  left: HuffmanNode | null
  right: HuffmanNode | null

  constructor(
    symbol: number | null,
    frequency: number,
    left: HuffmanNode | null = null,
    right: HuffmanNode | null = null
  ) {
    this.symbol = symbol
    this.frequency = frequency
    this.left = left
    this.right = right
  }

  isLeaf(): boolean {
    return this.left === null && this.right === null
  }
}

/**
 * Build a frequency table mapping each byte value to its occurrence count.
 */
export function buildFrequencyTable(data: Uint8Array): Map<number, number> {
  const freq = new Map<number, number>()
  for (const byte of data) {
    freq.set(byte, (freq.get(byte) || 0) + 1)
  }
  return freq
}

/**
 * Build a Huffman tree from a frequency table.
 * Returns the root node of the tree, or null if the frequency table is empty.
 */
export function buildHuffmanTree(frequencies: Map<number, number>): HuffmanNode | null {
  if (frequencies.size === 0) {
    return null
  }

  // Create leaf nodes and insert into a priority queue (min-heap by frequency).
  const nodes: HuffmanNode[] = []
  for (const [symbol, freq] of frequencies) {
    nodes.push(new HuffmanNode(symbol, freq))
  }

  // Sort descending so we can pop from the end (cheapest operation).
  nodes.sort((a, b) => b.frequency - a.frequency)

  // Special case: single unique symbol.
  if (nodes.length === 1) {
    const leaf = nodes[0]
    return new HuffmanNode(null, leaf.frequency, leaf)
  }

  while (nodes.length > 1) {
    const right = nodes.pop()!
    const left = nodes.pop()!
    const parent = new HuffmanNode(null, left.frequency + right.frequency, left, right)

    // Insert parent back in sorted position (binary search for efficiency).
    let lo = 0
    let hi = nodes.length
    while (lo < hi) {
      const mid = (lo + hi) >>> 1
      if (nodes[mid].frequency > parent.frequency) {
        lo = mid + 1
      } else {
        hi = mid
      }
    }
    nodes.splice(lo, 0, parent)
  }

  return nodes[0]
}

/**
 * Generate Huffman codes by traversing the tree.
 * Returns a map from byte value to its binary code string (e.g. "0110").
 */
export function generateCodes(root: HuffmanNode | null): Map<number, string> {
  const codes = new Map<number, string>()
  if (!root) return codes

  function traverse(node: HuffmanNode, code: string): void {
    if (node.isLeaf() && node.symbol !== null) {
      codes.set(node.symbol, code || '0')
      return
    }
    if (node.left) traverse(node.left, code + '0')
    if (node.right) traverse(node.right, code + '1')
  }

  traverse(root, '')
  return codes
}

/**
 * Serialized Huffman-encoded data, containing everything needed to decode.
 */
export interface HuffmanEncoded {
  /** The encoded bit data packed into bytes. */
  data: Uint8Array
  /** Number of padding bits in the last byte. */
  paddingBits: number
  /** The code table mapping byte values to bit strings, for reconstruction. */
  codes: Map<number, string>
  /** Original data length in bytes. */
  originalLength: number
}

/**
 * Encode a Uint8Array using Huffman coding.
 * Returns a HuffmanEncoded object containing the compressed data and metadata.
 */
export function encode(input: Uint8Array): HuffmanEncoded {
  if (input.length === 0) {
    return {
      data: new Uint8Array(0),
      paddingBits: 0,
      codes: new Map(),
      originalLength: 0
    }
  }

  const frequencies = buildFrequencyTable(input)
  const tree = buildHuffmanTree(frequencies)
  const codes = generateCodes(tree)

  const writer = new BitWriter()
  for (const byte of input) {
    const code = codes.get(byte)
    if (!code) {
      throw new Error(`No Huffman code for byte ${byte}`)
    }
    writer.writeBits(code)
  }

  const [data, paddingBits] = writer.flush()
  return { data, paddingBits, codes, originalLength: input.length }
}

/**
 * Decode Huffman-encoded data back to the original Uint8Array.
 */
export function decode(encoded: HuffmanEncoded): Uint8Array {
  if (encoded.originalLength === 0) {
    return new Uint8Array(0)
  }

  // Build a reverse lookup tree from codes for efficient decoding.
  const root = new HuffmanNode(null, 0)
  for (const [symbol, code] of encoded.codes) {
    let node = root
    for (const bit of code) {
      if (bit === '0') {
        if (!node.left) node.left = new HuffmanNode(null, 0)
        node = node.left
      } else {
        if (!node.right) node.right = new HuffmanNode(null, 0)
        node = node.right
      }
    }
    node.symbol = symbol
  }

  const reader = new BitReader(encoded.data, encoded.paddingBits)
  const output: number[] = []

  while (output.length < encoded.originalLength) {
    let node = root
    while (!node.isLeaf()) {
      const bit = reader.readBit()
      if (bit === 0) {
        if (!node.left) throw new Error('Invalid encoded data: missing left child')
        node = node.left
      } else {
        if (!node.right) throw new Error('Invalid encoded data: missing right child')
        node = node.right
      }
    }
    if (node.symbol === null) {
      throw new Error('Invalid encoded data: leaf node has no symbol')
    }
    output.push(node.symbol)
  }

  return new Uint8Array(output)
}
