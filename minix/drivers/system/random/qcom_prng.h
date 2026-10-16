/*
qcom_prng.h

The PRNG of a Qualcomm SoC as a source of the pool (the Mi Pad 4).  The
contract is trng.h's: init says OK or ENODEV (no such block, the ordinary
answer), feed puts a portion into the pool and returns its size.
*/

#ifndef QCOM_PRNG_H
#define QCOM_PRNG_H

int qprng_init(void);
size_t qprng_feed(void);

#endif /* QCOM_PRNG_H */
