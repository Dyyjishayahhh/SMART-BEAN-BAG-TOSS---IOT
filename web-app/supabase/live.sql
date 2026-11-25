begin;

create table if not exists public.ishoot_live_channels (
  id smallint primary key check (id = 1),
  state_topic text not null,
  command_topic text not null
);

alter table public.ishoot_live_channels enable row level security;
revoke all on public.ishoot_live_channels from public, anon, authenticated, service_role;

insert into public.ishoot_live_channels (id, state_topic, command_topic)
  values (1, 'ishoot-state-' || gen_random_uuid()::text, 'ishoot-command-' || gen_random_uuid()::text)
  on conflict do nothing;

create or replace function public.ishoot_live_topics()
returns jsonb language sql stable security definer set search_path = '' as $$
  select jsonb_build_object('state', state_topic, 'command', command_topic)
  from public.ishoot_live_channels where id = 1
$$;

revoke all on function public.ishoot_live_topics() from public, anon, authenticated;
grant execute on function public.ishoot_live_topics() to service_role;

-- The app and cabinet receive different unguessable topics through their existing
-- authenticated endpoints. Subscribers can read wake-ups, and only the database sends them.
create or replace function public.ishoot_live_topic_allowed(p_topic text)
returns boolean language sql stable security definer set search_path = '' as $$
  select exists (
    select 1 from public.ishoot_live_channels
    where id = 1 and p_topic in (state_topic, command_topic)
  )
$$;

revoke all on function public.ishoot_live_topic_allowed(text) from public, authenticated;
grant execute on function public.ishoot_live_topic_allowed(text) to anon;

drop policy if exists "I-Shoot Mo receive live signals" on realtime.messages;
create policy "I-Shoot Mo receive live signals" on realtime.messages
  for select to anon
  using (extension = 'broadcast' and topic = realtime.topic() and public.ishoot_live_topic_allowed(realtime.topic()));

create or replace function public.ishoot_signal_change()
returns trigger language plpgsql security definer set search_path = '' as $$
declare
  channels public.ishoot_live_channels%rowtype;
begin
  select * into channels from public.ishoot_live_channels where id = 1;

  if new.pending is not null and new.pending->>'id' is distinct from old.pending->>'id' then
    perform realtime.send('{}'::jsonb, 'changed', channels.command_topic, true);
  end if;

  if ((new.board->'state') - 'elapsedMs') is distinct from ((old.board->'state') - 'elapsedMs')
     or new.receipt is distinct from old.receipt
     or (new.seen_at is not null and (old.seen_at is null or new.seen_at - old.seen_at > interval '17 seconds')) then
    perform realtime.send('{}'::jsonb, 'changed', channels.state_topic, true);
  end if;

  return new;
exception when others then
  raise warning 'I-Shoot Mo live signal unavailable (%)', sqlstate;
  return new;
end;
$$;

revoke all on function public.ishoot_signal_change() from public, anon, authenticated, service_role;
drop trigger if exists ishoot_live_board_change on public.ishoot_board;
create trigger ishoot_live_board_change
  after update on public.ishoot_board
  for each row execute function public.ishoot_signal_change();

notify pgrst, 'reload schema';
commit;
