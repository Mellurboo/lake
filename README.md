# Lake

The new generation chat client infrastructure 

## Quickstart
Server:
```
./nob keygen run -- ./userA
./nob db-utils run -- -u ./userA.pub userA
./nob server run
```
Client:
```
./nob client run -- -key ./userA
```
