TODO
====

- Create action_nat.
- Create action_out.
- Run statistics on master core.
- Parse configuration dynamically.
- Handle SIGUSR1 and SIGUSR2 to reload configuration file and get queues
  statistics.
- Currently, configuration is allocated in the stack of main(). To improve
  performances, it should be duplicated once per memory socket.
