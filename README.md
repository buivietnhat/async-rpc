# asynchronous remote procedure call (RPC) library

## Project Overview

This project was inspired by three main ideas:

- Providing exactly-once semantics for applications, especially in distributed systems.
- Implementing transmission control (packet resend, filtering) at the application layer to enable the use of any transport layer like TCP or UDP.
- Implementing asynchronous network RPC calls to provide an easy-to-use RPC interface and mitigate network call stalls. 
