TODO
====

- Create action_nat.
- Create action_out.
- Run statistics on master core.
- Handle SIGUSR1 and SIGUSR2 to reload configuration file and get queues
  statistics.
- Add a way to set core->need_reload_conf to 1.
- Test configuration is valid. Ensure parameters aren't updated during reload:
  for instance, it is possible to update network rules but not ports infos.
- During configuration parsing, ensure ctx->current_rule and
  ctx->current_action don't overflow.
- Free old configuration before parsing the new one.
