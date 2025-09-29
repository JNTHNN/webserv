#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Testeur POST Chunked Simplifié
=============================

Version simple avec formulaires de test
"""

import os
import sys
import html

def main():
    # Headers HTTP
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write("\r\n")
    
    method = os.environ.get('REQUEST_METHOD', 'GET')
    content_length = os.environ.get('CONTENT_LENGTH', '0')
    
    # Lire le body si POST
    body = ""
    if method == 'POST':
        if content_length and content_length.isdigit() and int(content_length) > 0:
            body = sys.stdin.read(int(content_length))
        else:
            body = sys.stdin.read()
    
    print("""<!DOCTYPE html>
<html>
<head>
    <title>🧪 Test POST Chunked Simple</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }
        .result { background: #f0f8ff; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
        button { background: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 5px; margin: 5px; cursor: pointer; }
        pre { background: #f8f9fa; padding: 10px; border-radius: 3px; overflow-x: auto; }
    </style>
</head>
<body>
    <h1>🧪 Testeur POST Chunked</h1>""")
    
    if method == 'POST':
        status = "success" if body else "error"
        message = f"✅ Body reçu: {len(body)} bytes" if body else "❌ Aucun body reçu"
        
        print(f"""
    <div class="result {status}">
        <h3>Résultat du test:</h3>
        <p><strong>{message}</strong></p>
        <p>Method: {method}</p>
        <p>Content-Length: {content_length}</p>
        {f'<pre>{body}</pre>' if body else ''}
    </div>""")
    

    # Affichage du résultat GET/POST classique (hors chunked)
    if method == 'GET' or (method == 'POST' and not os.environ.get('HTTP_TRANSFER_ENCODING')):
        query = os.environ.get('QUERY_STRING', '')
        form_data = ''
        if method == 'POST' and not body:
            # Si pas de body, essayer de lire depuis stdin (cas POST classique)
            content_length = os.environ.get('CONTENT_LENGTH')
            if content_length and content_length.isdigit():
                form_data = sys.stdin.read(int(content_length))
        else:
            form_data = body
        print(f"""
        <div class='result info'>
            <h3>Test CGI Classique</h3>
            <p><strong>Method:</strong> {method}</p>
            <p><strong>Query:</strong> {html.escape(query)}</p>
            <p><strong>Body:</strong> {html.escape(form_data)}</p>
        </div>
        """)

    print("""
    <h2>Formulaires CGI classiques</h2>
    <form method="GET">
        <input name="test" placeholder="Test GET">
        <button>Envoyer GET</button>
    </form>
    <form method="POST">
        <input name="data" placeholder="Test POST">
        <button>Envoyer POST</button>
    </form>
    """)

    print("""
    <h2>Tests disponibles:</h2>
    
    <button onclick="testSimple()">📝 Test Simple (5 bytes)</button>
    <button onclick="testMultiple()">📚 Test Multiple Chunks</button>
    <button onclick="testLarge()">📈 Test Gros Chunk</button>
    
    <div id="status" style="margin: 20px 0;"></div>
    
    <script>
        function showStatus(msg, isError = false) {
            const status = document.getElementById('status');
            status.innerHTML = `<div class="result ${isError ? 'error' : 'success'}">${msg}</div>`;
        }
        
        async function sendChunkedRequest(chunks) {
            showStatus('🔄 Envoi de la requête chunked...');
            
            try {
                // Construire le body chunked
                let body = '';
                for (let chunk of chunks) {
                    body += chunk.length.toString(16) + '\\r\\n';
                    body += chunk + '\\r\\n';
                }
                body += '0\\r\\n\\r\\n';
                
                console.log('Sending chunked body:', body);
                
                const response = await fetch(window.location.href, {
                    method: 'POST',
                    headers: {
                        'Transfer-Encoding': 'chunked',
                        'Content-Type': 'text/plain'
                    },
                    body: body
                });
                
                if (response.ok) {
                    showStatus('✅ Requête envoyée, rechargement...');
                    setTimeout(() => window.location.reload(), 50000);
                } else {
                    throw new Error(`HTTP ${response.status}`);
                }
                
            } catch (error) {
                showStatus(`❌ Erreur: ${error.message}`, true);
            }
        }
        
        function testSimple() {
            sendChunkedRequest(['hello']);
        }
        
        function testMultiple() {
            sendChunkedRequest(['hello', ' ', 'world', '!']);
        }
        
        function testLarge() {
            sendChunkedRequest(['A'.repeat(500), 'B'.repeat(500)]);
        }
    </script>    
</body>
</html>""")

if __name__ == "__main__":
    main()