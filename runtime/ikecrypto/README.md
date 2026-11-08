# Retained crypto source provenance (historical IKE context)

The IKE names below describe actual retained code, not an active/future
protocol target. The 2026-11-08 decision excludes IKEv2; see the normative
[planned ESP-SA-BUNDLE](../../docs/ESP-SA-BUNDLE.md). Existing AES-GCM is
potential reuse material for future userspace ESP, not proof of that service.
This documentation task changes no crypto source, build or license.

BearSSL 0.6, unmodified subset from https://bearssl.org/bearssl-0.6.tar.gz
Archive SHA-256: 6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14
License: bearssl/LICENSE.txt (MIT).

Only P-256 m31, constant-time AES-CTR, constant-time GHASH, GCM and SHA-256
are linked. No TLS, certificates, allocation, OS entropy or dynamic modules.
ikecrypto.c supplies userspace-only HMAC and P-256 wrappers. espcrypto.c
supplies shared AES-GCM and erasure. The kernel build explicitly includes
only espcrypto.c and AES/GHASH/GCM vendor units; no IKE/P-256 is linked there.
