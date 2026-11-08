// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_AUDIO_BANK_NATIVE_H
#define WOLF2009_SOURCE_AUDIO_BANK_NATIVE_H
class idFile;

// Core-private whole-bank service for the explicit Category8SndsMp3V1
// scripted-voice profile. The separately linked game module reaches it only
// through ordinary idFile operations on reserved paths; API8 is unchanged.
//
//   wolf2009/_sound/bank/v1/request/<locale>/<definition>
//     Expands the definition, snapshots and validates the complete SPK, mounts
//     and pins it under a fresh label, registers EVERY SNDS member in the real
//     native sound cache and attaches a strict full-decode receipt to each. The
//     returned file is either a lease (manifest state "ready"; holding the file
//     open retains the bank) or a refusal (state "failed"; owns nothing).
//     A live, still-verified bank of the same key is joined, never redone.
//   wolf2009/_sound/bank/v1/status/<generation>
//     Re-observes a live bank's mount epoch and every cache identity. Returns
//     state "ready" or "lost"; an unknown or released generation opens nothing.
//
// Engine resource thread only. Ready is committed once, after all members.
bool Wolf2009_IsSourceAudioBankRequest(const char* path);
idFile* Wolf2009_OpenSourceAudioBank(const char* path);

// Diagnostics for teardown and QA; no ownership is transferred.
unsigned Wolf2009_LiveSourceAudioBanks();
unsigned long long Wolf2009_SourceAudioBankRegistrations();
unsigned long long Wolf2009_SourceAudioBankRefusals();
#endif
