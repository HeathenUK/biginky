#!/bin/bash
# Generate a self-signed certificate for ESP32-P4 HTTPS server
# This creates a certificate valid for 10 years

echo "Generating self-signed certificate for ESP32-P4 HTTPS server..."

# Generate private key (2048-bit RSA)
openssl genrsa -out server_key.pem 2048

# Generate self-signed certificate (valid for 10 years)
openssl req -new -x509 -key server_key.pem -out server_cert.pem -days 3650 \
    -subj "/C=US/ST=State/L=City/O=ESP32-P4/CN=esp32.local"

echo ""
echo "Certificate generated successfully!"
echo "Files created:"
echo "  - server_key.pem (private key - KEEP SECRET!)"
echo "  - server_cert.pem (certificate)"
echo ""
echo "Next steps:"
echo "1. The certificate will be embedded in the code"
echo "2. Browsers will show a security warning (this is normal for self-signed certs)"
echo "3. You can click 'Advanced' -> 'Proceed to site' to continue"
echo ""
echo "To view certificate details:"
echo "  openssl x509 -in server_cert.pem -text -noout"


