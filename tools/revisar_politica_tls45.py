#!/usr/bin/env python3
"""Static sanity checks, not substitutes for certificate verification tests."""
from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'kernel/mios_tls4_mbedtls36.inc').read_text()
checks={
 'Verify REQUIRED':'MBEDTLS_SSL_VERIFY_REQUIRED',
 'Hostname + SNI':'mbedtls_ssl_set_hostname',
 'Peer cert':'mbedtls_ssl_get_peer_cert',
 'Verification result':'mbedtls_ssl_get_verify_result',
 'Trust anchors':'mbedtls_ssl_conf_ca_chain',
 'Server certificate dates':'MBEDTLS_HAVE_TIME_DATE',
 'UTC conversion':'mbedtls_platform_gmtime_r',
 'Entropy source':'MBEDTLS_ENTROPY_SOURCE_STRONG',
 'Network callbacks':'mbedtls_ssl_set_bio',
}
for k, v in checks.items():
    assert v in s,k
    print('POLICY SOURCE CHECK:',k,'OK')
assert 'MBEDTLS_SSL_VERIFY_NONE' not in s
print('ATENCION: revision estatica; NO comprueba handshake ni certificados reales')
