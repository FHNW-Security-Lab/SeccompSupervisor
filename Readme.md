# Syscall Supervisor with User Decisions

This project implements a system call supervision framework that allows users to interactively control network-related system calls made by programs. It consists of multiple components that work together to provide user-controlled system call filtering.

## Components

### 1. Supervisor (supervisor.c)
The main supervision program that uses seccomp and ptrace to monitor and control system calls made by other programs. It intercepts network-related syscalls (socket, connect) and consults with the user-tool before allowing or denying them.

### 2. User Tool (user-tool.c)
A daemon process that handles user interaction. It prompts the user for decisions when the supervisor intercepts a system call, allowing the user to permit or deny the operation.

### 3. Test Programs
- **test-server.c**: A simple TCP server for testing
- **test-client.c**: A simple TCP client that attempts to connect to the server

## Building

The project uses a Makefile for building all components. To build everything:

```bash
make
```

This will create the following executables:
- supervisor
- user-tool
- test-server
- test-client

To clean built files:
```bash
make clean
```

## Usage

1. First, start the user-tool daemon:
```bash
./user-tool
```

2. In another terminal, start the test server (optional, for testing network connections):
```bash
./test-server
```

3. Run your program under supervision:
```bash
sudo ./supervisor ./your-program
```

For example, to monitor the test client:
```bash
sudo ./supervisor ./test-client
```

The supervisor will:
- Intercept network-related system calls
- Consult with the user-tool
- Allow or deny the system call based on user input

## Example Output

When running the test client under supervision:

```
[Supervisor] Starting supervision of program: ./test-client
[Supervisor] Full program path: /path/to/test-client
[Supervisor] Connecting to user-tool daemon...
[Supervisor] Successfully connected to user-tool daemon
[Supervisor] Starting monitored program (PID: 1234)
[Supervisor] Beginning system call monitoring...

[Supervisor] Intercepted socket system call
[Supervisor] Requesting permission for syscall socket (41)
```

The user-tool will then prompt:
```
Program is attempting to make a system call: socket (41)
Allow this operation? (y/n):
```

## Security Considerations

- The supervisor requires root privileges to use ptrace
- The Unix domain socket (/tmp/user_tool.sock) is used for communication between components
- The supervisor uses seccomp in filter mode to selectively intercept system calls
- Currently monitors network-related system calls (socket and connect)

## Requirements

- Linux operating system
- GCC compiler
- Root privileges for running the supervisor

## Technical Details

### System Call Interception
- Uses seccomp-bpf for system call filtering
- Uses ptrace for system call interception and modification
- Filters socket and connect system calls
- All other system calls are allowed by default

### Inter-Process Communication
- Uses Unix domain sockets for communication between supervisor and user-tool
- Simple text-based protocol for permission requests and responses

## Future Improvements

Potential areas for enhancement:
- Support for additional system calls
- More detailed system call information (arguments, context)
- Configuration file for default policies
- Logging system for audit trails
- Enhanced security measures for the Unix domain socket
- Support for different response policies (allow, deny, ask)

## Contributing

Feel free to open issues or submit pull requests to improve the project.

## License

This project is open source and available under the BSD 3-Clause License.
