import { inflateRawSync, inflateSync } from 'node:zlib';
import { openSync, readSync, closeSync, fstatSync } from 'node:fs';

const ZIP_END = 0x06054b50;
const WOLF_END = 0x48530a02;
export function need(condition, message) { if (!condition) throw new Error(message); }
export function range(buffer, offset, bytes, label) {
  need(Number.isSafeInteger(offset) && Number.isSafeInteger(bytes) && offset >= 0 && bytes >= 0 && offset + bytes <= buffer.length, `Truncated ${label} at ${offset}`);
}
export function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let i = 0; i < 8; ++i) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}

// The signature substitution is only accepted with a coherent single-disk
// central directory. No original archive is patched or written.
export function zipDirectory(buffer) {
  need(buffer.length >= 22, 'ZIP end record missing');
  let end = -1;
  for (let i = buffer.length - 22; i >= Math.max(0, buffer.length - 65557); --i) {
    const signature = buffer.readUInt32LE(i);
    if ((signature === ZIP_END || signature === WOLF_END) && i + 22 + buffer.readUInt16LE(i + 20) === buffer.length) { end = i; break; }
  }
  need(end >= 0, 'ZIP end record missing');
  const signature = buffer.readUInt32LE(end);
  need(buffer.readUInt16LE(end + 4) === 0 && buffer.readUInt16LE(end + 6) === 0, 'Multi-disk ZIP unsupported');
  const count = buffer.readUInt16LE(end + 10), cdBytes = buffer.readUInt32LE(end + 12), cdOffset = buffer.readUInt32LE(end + 16);
  need(buffer.readUInt16LE(end + 8) === count, 'ZIP entry counts disagree');
  need(count !== 0xffff && cdOffset !== 0xffffffff && cdBytes !== 0xffffffff, 'ZIP64 unsupported');
  need(cdOffset + cdBytes === end, 'ZIP central directory bounds disagree');
  let pos = cdOffset;
  const entries = [];
  for (let i = 0; i < count; ++i) {
    range(buffer, pos, 46, 'ZIP central entry');
    need(pos + 46 <= end && buffer.readUInt32LE(pos) === 0x02014b50, 'ZIP central entry signature invalid');
    const flags = buffer.readUInt16LE(pos + 8), method = buffer.readUInt16LE(pos + 10);
    const compressedBytes = buffer.readUInt32LE(pos + 20), bytes = buffer.readUInt32LE(pos + 24);
    const nameBytes = buffer.readUInt16LE(pos + 28), extraBytes = buffer.readUInt16LE(pos + 30), commentBytes = buffer.readUInt16LE(pos + 32);
    const localOffset = buffer.readUInt32LE(pos + 42);
    range(buffer, pos + 46, nameBytes + extraBytes + commentBytes, 'ZIP central variable data');
    need(pos + 46 + nameBytes + extraBytes + commentBytes <= end, 'ZIP central entry exceeds directory');
    need(buffer.readUInt16LE(pos + 34) === 0, 'ZIP entry disk unsupported');
    range(buffer, localOffset, 30, 'ZIP local entry');
    need(buffer.readUInt32LE(localOffset) === 0x04034b50, 'ZIP local entry signature invalid');
    const localNameBytes = buffer.readUInt16LE(localOffset + 26), localExtraBytes = buffer.readUInt16LE(localOffset + 28);
    const dataOffset = localOffset + 30 + localNameBytes + localExtraBytes;
    range(buffer, dataOffset, compressedBytes, 'ZIP compressed member');
    need(dataOffset + compressedBytes <= cdOffset, 'ZIP member overlaps central directory');
    const name = buffer.subarray(pos + 46, pos + 46 + nameBytes).toString('utf8');
    need(localNameBytes === nameBytes && buffer.subarray(localOffset + 30, localOffset + 30 + localNameBytes).equals(buffer.subarray(pos + 46, pos + 46 + nameBytes)), 'ZIP local/central names disagree');
    need(buffer.readUInt16LE(localOffset + 6) === flags && buffer.readUInt16LE(localOffset + 8) === method, 'ZIP local/central method or flags disagree');
    if (!(flags & 8)) need(buffer.readUInt32LE(localOffset + 14) === buffer.readUInt32LE(pos + 16) && buffer.readUInt32LE(localOffset + 18) === compressedBytes && buffer.readUInt32LE(localOffset + 22) === bytes, 'ZIP local/central CRC or sizes disagree');
    entries.push({ name, flags, method, bytes, compressedBytes, crc32: buffer.readUInt32LE(pos + 16), localOffset, dataOffset });
    pos += 46 + nameBytes + extraBytes + commentBytes;
  }
  need(pos === end, 'ZIP central size/count mismatch');
  return { footerSignature: signature.toString(16).padStart(8, '0'), footerOffset: end, footerType: signature === WOLF_END ? 'wolf2009-substituted-signature' : 'zip', centralOffset: cdOffset, centralBytes: cdBytes, entries };
}

export function zipMember(buffer, entry, maxBytes = 256 * 1024 * 1024) {
  need(!(entry.flags & 1), 'Encrypted ZIP unsupported');
  need(entry.bytes <= maxBytes, 'ZIP member size budget exceeded');
  const compressed = buffer.subarray(entry.dataOffset, entry.dataOffset + entry.compressedBytes);
  const data = entry.method === 0 ? compressed : entry.method === 8 ? inflateRawSync(compressed, { maxOutputLength: Math.max(1, maxBytes) }) : null;
  need(data !== null, `ZIP method ${entry.method} unsupported`);
  need(data.length === entry.bytes, 'ZIP member decompressed size mismatch');
  need(crc32(data) === entry.crc32, 'ZIP member CRC mismatch');
  return data;
}

function readExact(fd, offset, bytes) {
  const data = Buffer.alloc(bytes);
  let readBytes = 0;
  while (readBytes < bytes) {
    const count = readSync(fd, data, readBytes, bytes - readBytes, offset + readBytes);
    need(count > 0, `Truncated package at ${offset + readBytes}`);
    readBytes += count;
  }
  return data;
}
export function scanStreamPackage(path) {
  const fd = openSync(path, 'r');
  try {
    const bytes = fstatSync(fd).size;
    need(bytes >= 4, 'Package magic truncated');
    const version = readExact(fd, 0, 4).readUInt32LE();
    need(version === 300, `Package magic ${version} unsupported`);
    let offset = 4;
    const chunks = [];
    while (offset < bytes) {
      need(bytes - offset >= 12, `Package chunk header truncated at ${offset}`);
      const h = readExact(fd, offset, 12), type = Buffer.from(h.subarray(0, 4)).reverse().toString('ascii');
      const uncompressedBytes = h.readUInt32LE(4), compressedBytes = h.readUInt32LE(8);
      need(/^[A-Z0-9]{4}$/.test(type), `Package chunk tag invalid at ${offset}`);
      need(compressedBytes > 0 && offset + 12 + compressedBytes <= bytes, `Package chunk bounds invalid at ${offset}`);
      chunks.push({ type, offset, dataOffset: offset + 12, uncompressedBytes, compressedBytes });
      offset += 12 + compressedBytes;
    }
    return { version, bytes, chunks };
  } finally { closeSync(fd); }
}

export function streamPayload(data) {
  function align4(offset) {
    const aligned = Math.ceil(offset / 4) * 4;
    need(aligned <= data.length, 'Stream alignment exceeds payload');
    return aligned;
  }
  range(data, 0, 4, 'stream file count');
  const count = data.readUInt32LE();
  need(count <= Math.floor((data.length - 4) / 9), 'Stream file count exceeds table capacity');
  const files = [];
  let pos = 4;
  for (let i = 0; i < count; ++i) {
    const end = data.indexOf(0, pos);
    need(end >= pos && end - pos <= 4096, 'Stream name missing or excessive');
    need(end > pos, 'Stream file name empty');
    const name = data.subarray(pos, end).toString('utf8');
    range(data, end + 1, 8, 'stream file sizes');
    const bytes = data.readUInt32LE(end + 1), secondBytes = data.readUInt32LE(end + 5);
    need(bytes === secondBytes, `Stream duplicate sizes disagree for ${name}`);
    files.push({ name, bytes }); pos = end + 9;
  }
  pos = align4(pos);
  for (const entry of files) {
    range(data, pos, entry.bytes, `stream file ${entry.name}`);
    entry.dataOffset = pos;
    entry.prefixHex = data.subarray(pos, pos + Math.min(entry.bytes, 32)).toString('hex');
    entry.prefixAscii = data.subarray(pos, pos + Math.min(entry.bytes, 32)).toString('latin1').replace(/[^\x20-\x7e]/g, '.');
    pos = align4(pos + entry.bytes);
  }
  need(pos === data.length, 'Stream payload contains unexplained trailing data');
  return { count, files, accountedBytes: pos, trailingBytes: 0 };
}

export function sampleStreamPackage(path, structure = scanStreamPackage(path), maxBytes = 256 * 1024 * 1024) {
  const fd = openSync(path, 'r');
  try {
    return structure.chunks.map(chunk => {
      need(chunk.uncompressedBytes <= maxBytes && chunk.compressedBytes <= maxBytes, 'Stream sample size budget exceeded');
      const compressed = readExact(fd, chunk.dataOffset, chunk.compressedBytes);
      const inflated = inflateSync(compressed, { maxOutputLength: maxBytes, info: true });
      need(inflated.engine.bytesWritten === compressed.length, 'Stream compressed payload contains trailing data');
      const payload = inflated.buffer;
      need(payload.length === chunk.uncompressedBytes, 'Stream decompressed size mismatch');
      const table = streamPayload(payload);
      return { ...chunk, files: table.count, payloadBytes: payload.length, tableValidated: true, samples: table.files.slice(0, 8) };
    });
  } finally { closeSync(fd); }
}
