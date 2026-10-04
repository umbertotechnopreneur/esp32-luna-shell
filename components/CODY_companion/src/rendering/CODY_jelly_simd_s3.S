/* ESP32-S3 PIE kernel. Arithmetic follows the ESP32-S3 TRM v1.8, chapter 1.
 * Two RGBA pixels occupy eight unsigned 16-bit lanes after ZIP widening.
 * Every lerp computes (first*(256-weight) + second*weight + 128) >> 8.
 * QACC's 40-bit lanes retain the full sum; only the result is narrowed.
 * The maximum sum is 65,408 and the extracted value is at most 255:
 * signed SRCMB saturation cannot change any valid result.
 * QR/QACC are caller-saved CP3 state managed by the IDF Xtensa port.
 * Task context only. Does not call functions, mask interrupts or change CPENABLE.
 */

    .text
    .align 4
    .global cody_jelly_bilinear_s3
    .type cody_jelly_bilinear_s3, @function

// pairs (a2): 16-byte-aligned JellySimdPair records, 48 bytes each.
// output (a3): 16-byte-aligned RGBA8 scratch; writes eight bytes per pair only.
// pair_count (a4): Number of complete pairs; zero is a no-op.
// constants (a5): Vertical weights at +0, rounding lanes at +16, aligned to 16.
cody_jelly_bilinear_s3:
    entry a1, 32
    beqz a4, .Ljelly_done
    mov a8, a2
    mov a9, a3
    mov a10, a5
    movi a11, 8
    // Native stores that filled scratch must retire before PIE loads consume it.
    memw
    ee.vld.128.ip q6, a10, 16
    ee.vld.128.ip q7, a10, 16

    loopnez a4, .Ljelly_pairs_end
        ee.vld.128.ip q0, a8, 16
        ee.vld.128.ip q2, a8, 16
        ee.vld.128.ip q4, a8, 16
        ee.zero.q q1
        ee.zero.q q3
        ee.vzip.8 q0, q1
        ee.vzip.8 q2, q3
        // q0/q2 are upper neighbours; q1/q3 are lower neighbours.
        ee.vadds.s16 q5, q7, q7
        ee.vsubs.s16 q5, q5, q4
        ee.mov.u16.qacc q7
        ee.vmulas.u16.qacc q0, q5
        ee.vmulas.u16.qacc q2, q4
        ee.srcmb.s16.qacc q0, a11, 0
        ee.mov.u16.qacc q7
        ee.vmulas.u16.qacc q1, q5
        ee.vmulas.u16.qacc q3, q4
        ee.srcmb.s16.qacc q1, a11, 0
        // Horizontal results have already been rounded, matching both C lerps.
        ee.vadds.s16 q5, q7, q7
        ee.vsubs.s16 q5, q5, q6
        ee.mov.u16.qacc q7
        ee.vmulas.u16.qacc q0, q5
        ee.vmulas.u16.qacc q1, q6
        ee.srcmb.s16.qacc q0, a11, 0
        ee.zero.q q1
        ee.vunzip.8 q0, q1
        ee.vst.l.64.ip q0, a9, 8
.Ljelly_pairs_end:
    memw
.Ljelly_done:
    retw.n
    .size cody_jelly_bilinear_s3, .-cody_jelly_bilinear_s3
