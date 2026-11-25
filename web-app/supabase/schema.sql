begin;

create table if not exists public.ishoot_board (
  id smallint primary key check (id = 1),
  seen_at timestamptz,
  board jsonb not null default '{}'::jsonb,
  pending jsonb,
  receipt jsonb
);

create table if not exists public.ishoot_rounds (
  id text primary key,
  player_key text not null,
  status text not null check (status in ('active', 'complete', 'canceled')),
  payload jsonb not null,
  updated_at timestamptz not null default now()
);

create index if not exists ishoot_rounds_player_recent
  on public.ishoot_rounds (player_key, updated_at desc) where status = 'complete';

alter table public.ishoot_board enable row level security;
alter table public.ishoot_rounds enable row level security;
revoke all on public.ishoot_board, public.ishoot_rounds from public, anon, authenticated, service_role;
grant select on public.ishoot_board, public.ishoot_rounds to service_role;
insert into public.ishoot_board (id) values (1) on conflict do nothing;

create or replace function public.ishoot_queue(p_command jsonb)
returns text language plpgsql security definer set search_path = '' as $$
declare
  device public.ishoot_board%rowtype;
  state jsonb;
  phase text;
  kind text := p_command->>'kind';
begin
  select * into device from public.ishoot_board where id = 1 for update;
  if device.seen_at is null or device.seen_at < now() - interval '17 seconds' then
    return 'offline';
  end if;
  if device.pending is not null and (device.pending->>'expiresAt')::timestamptz > now() then
    return 'busy';
  end if;
  state := device.board->'state';
  phase := state->>'phase';
  if kind = 'start' and (phase is null or phase = 'playing') then
    return 'stale';
  end if;
  if kind in ('reset', 'miss') and (
    (state->>'roundId') is distinct from (p_command->>'roundId')
    or phase is null or phase = 'ready'
    or (kind = 'miss' and (phase <> 'playing' or state->>'count' <> p_command->>'count'))
  ) then
    return 'stale';
  end if;
  update public.ishoot_board set pending = p_command where id = 1;
  return 'queued';
end;
$$;

create or replace function public.ishoot_sync(p_board jsonb, p_rounds jsonb, p_ack jsonb)
returns jsonb language plpgsql security definer set search_path = '' as $$
declare
  device public.ishoot_board%rowtype;
  round_payload jsonb;
begin
  select * into device from public.ishoot_board where id = 1 for update;

  if device.pending->>'id' = p_ack->>'id' then
    device.receipt := p_ack;
    device.pending := null;
  elsif (device.pending->>'expiresAt')::timestamptz <= now() then
    device.receipt := jsonb_build_object('id', device.pending->>'id', 'result', 'expired');
    device.pending := null;
  end if;

  for round_payload in select value from jsonb_array_elements(p_rounds) loop
    insert into public.ishoot_rounds as saved (id, player_key, status, payload)
    values (round_payload->>'id', lower(round_payload->>'player'), round_payload->>'status', round_payload)
    on conflict (id) do update set
      status = excluded.status, payload = excluded.payload, updated_at = now()
    where saved.status = 'active' and saved.player_key = excluded.player_key
      and saved.payload is distinct from excluded.payload
      and jsonb_array_length(saved.payload->'attempts') <= jsonb_array_length(excluded.payload->'attempts');
  end loop;

  update public.ishoot_board
    set board = p_board, seen_at = now(), pending = device.pending, receipt = device.receipt
    where id = 1;
  return device.pending;
end;
$$;

revoke all on function public.ishoot_queue(jsonb) from public, anon, authenticated;
revoke all on function public.ishoot_sync(jsonb, jsonb, jsonb) from public, anon, authenticated;
grant execute on function public.ishoot_queue(jsonb) to service_role;
grant execute on function public.ishoot_sync(jsonb, jsonb, jsonb) to service_role;
notify pgrst, 'reload schema';
commit;
