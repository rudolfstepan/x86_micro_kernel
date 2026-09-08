// JS2 example: arguments are data; no file/network/process authority.
print('Hello from REIST JavaScript');
console.log('Arguments:', scriptArgs.slice(1).join(' '));
console.error('Console streams stay brokered.');
reist.setExitCode(0);
