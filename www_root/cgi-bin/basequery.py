#!/usr/bin/env python3

import cgi
import sys

try:
    my_id = int(cgi.FieldStorage().getvalue("my_id"))

    page = '''<!doctype html>
    <html><head><title>CGI Query</title></head>
    <body><h2>CGI Response my_id: {}</h2></body></html>'''.format(my_id)

    print("Content-Type:text/html\n\r", end='')
    print("Content-Length:{}\n\r\n\r".format(len(page)), end='')
    print(page, end='')

except:
    sys.exit(1)
