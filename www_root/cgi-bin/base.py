#!/usr/bin/env python3
page = '''<!doctype html>
<html>
    <head>
        <title>CGI</title>
    </head>
    <body>
        <h2>Welcome to my CGI</h2>
        This is some text
        <br></br>
    </body>
</html>'''

print("Content-Type:text/html\r\n", end='')
print("Content-Length:{}\r\n\r\n".format(len(page)), end='')

print(page, end='')
