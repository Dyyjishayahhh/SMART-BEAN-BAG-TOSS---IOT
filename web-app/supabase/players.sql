begin;

create table if not exists public.ishoot_players (
  id uuid primary key default gen_random_uuid(),
  username text not null,
  username_key text not null unique check (username_key = lower(username)),
  password_hash text not null
);
create table if not exists public.ishoot_auth_attempts (
  key text primary key,
  window_start timestamptz not null,
  attempts integer not null
);
create table if not exists public.ishoot_start_tickets (
  id text primary key,
  owner_key text not null,
  player text not null,
  game text not null,
  round_id text unique
);
alter table public.ishoot_start_tickets add column if not exists versus boolean not null default false;
alter table public.ishoot_start_tickets add column if not exists guest_rank_key text;

create table if not exists public.ishoot_guest_scores (
  round_id text primary key,
  participant_key text not null,
  player text not null,
  score integer not null,
  completed_at timestamptz not null default now()
);
alter table public.ishoot_guest_scores add column if not exists corrections integer not null default 0;

alter table public.ishoot_players enable row level security;
alter table public.ishoot_auth_attempts enable row level security;
alter table public.ishoot_start_tickets enable row level security;
alter table public.ishoot_guest_scores enable row level security;
revoke all on public.ishoot_players, public.ishoot_auth_attempts, public.ishoot_start_tickets, public.ishoot_guest_scores
  from public, anon, authenticated, service_role;
grant select, insert on public.ishoot_players to service_role;

alter table public.ishoot_rounds add column if not exists owner_key text;
alter table public.ishoot_rounds add column if not exists start_id text;
alter table public.ishoot_rounds add column if not exists score integer not null default 0;
update public.ishoot_rounds as round
  set score = (
    select coalesce(sum((attempt->>0)::int), 0)
      from jsonb_array_elements(round.payload->'attempts') as t(attempt)
     where attempt->>0 = attempt->>1
  )
  where round.status = 'complete' and round.score = 0;

create index if not exists ishoot_rounds_owner_recent
  on public.ishoot_rounds (owner_key, updated_at desc) where status = 'complete';
create index if not exists ishoot_rounds_chase_score
  on public.ishoot_rounds (score desc)
  where status = 'complete' and payload->>'game' = 'chase' and payload->>'type' = 'competition';

create or replace function public.ishoot_auth_budget(p_browser text, p_username text)
returns boolean language plpgsql security definer set search_path = '' as $$
declare
  bucket timestamptz := date_bin('10 minutes', now(), '2000-01-01 00:00:00Z'::timestamptz);
  browser_attempts integer;
  name_attempts integer;
begin
  insert into public.ishoot_auth_attempts as recorded values ('browser:' || p_browser, bucket, 1)
  on conflict (key) do update set
    attempts = case when recorded.window_start = bucket then least(recorded.attempts + 1, 19) else 1 end,
    window_start = bucket
  returning attempts into browser_attempts;
  if browser_attempts > 18 then return false; end if;

  insert into public.ishoot_auth_attempts as recorded values ('name:' || p_username, bucket, 1)
  on conflict (key) do update set
    attempts = case when recorded.window_start = bucket then least(recorded.attempts + 1, 9) else 1 end,
    window_start = bucket
  returning attempts into name_attempts;
  return name_attempts <= 8;
end;
$$;

create or replace function public.ishoot_chase_leaderboard()
returns table(player text, score integer, rank bigint)
language sql stable security definer set search_path = '' as $$
  with eligible as (
    select coalesce(round.owner_key, 'legacy:' || round.player_key) as participant,
      case when round.owner_key like 'a:%' or round.payload->>'player' = 'Guest'
        then round.payload->>'player' else (round.payload->>'player') || ' (Guest)' end as player,
      round.score, round.updated_at, round.id
    from public.ishoot_rounds as round
    where round.status = 'complete' and round.payload->>'game' = 'chase'
      and round.payload->>'type' = 'competition'
    union all
    select guest.participant_key,
      case when guest.player = 'Guest' then guest.player else guest.player || ' (Guest)' end,
      guest.score, guest.completed_at, guest.round_id
    from public.ishoot_guest_scores as guest
  ), best as (
    select distinct on (participant) player, score, updated_at, id
    from eligible
    order by participant, score desc, updated_at, id
  )
  select best.player, best.score, dense_rank() over (order by best.score desc)
  from best
  order by best.score desc, best.updated_at, best.id
  limit 25
$$;

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
  if kind = 'start' then
    if phase is null or phase = 'playing' then return 'stale'; end if;
    if p_command->>'versus' = 'true' and p_command->>'game' = 'memory' then return 'stale'; end if;
    if (p_command->>'game' = 'memory' and coalesce(device.board->>'version', '') not in ('3', '4'))
       or (p_command->>'versus' = 'true' and coalesce(device.board->>'version', '') not in ('2', '3', '4')) then
      return 'upgrade';
    end if;
    if p_command->>'roundId' is not null and
       (phase <> 'results' or state->>'roundId' is distinct from p_command->>'roundId') then
      return 'stale';
    end if;
    if p_command->>'ownerKey' is not null then
      insert into public.ishoot_start_tickets (id, owner_key, player, game, versus, guest_rank_key)
        values (p_command->>'id', p_command->>'ownerKey', p_command->>'player', p_command->>'game',
          coalesce((p_command->>'versus')::boolean, false), p_command->>'guestRankKey');
    end if;
  end if;
  if kind = 'correct' then
    if device.board->>'version' is distinct from '4' then return 'upgrade'; end if;
    if phase is null or phase = 'ready' or state->>'previewing' = 'true'
       or state->>'roundId' is distinct from p_command->>'roundId'
       or state->>'count' is distinct from p_command->>'count'
       or state->'lastHit'->>'number' is distinct from p_command->>'attempt' then
      return 'stale';
    end if;
  end if;
  if kind in ('reset', 'miss') and (
    (state->>'roundId') is distinct from (p_command->>'roundId')
    or phase is null or phase = 'ready'
    or (kind = 'miss' and (phase <> 'playing' or state->>'previewing' = 'true'
      or state->>'count' <> p_command->>'count'))
  ) then
    return 'stale';
  end if;
  if kind in ('reset', 'miss') and device.board->>'versusOwner' is not null
     and device.board->>'versusOwner' is distinct from p_command->>'visitorKey' then
    return 'owned';
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
  round_id text;
  board_round_id text := p_board->'state'->>'roundId';
  board_start_id text;
  fallback_start_id text;
  candidate text;
  round_owner text;
  round_versus boolean;
  guest_key text;
  confirmed_start text;
  round_score integer;
begin
  select * into device from public.ishoot_board where id = 1 for update;
  p_board := p_board - 'versusOwner' - 'memoryOwner';
  p_board := p_board #- '{state,versus}';
  p_board := p_board #- '{state,guestRound}';
  if device.pending->>'id' = p_ack->>'id' then
    if device.pending->>'kind' = 'start' and p_ack->>'result' = 'applied'
       and coalesce(p_board->>'version', '') not in ('2', '3', '4') then
      fallback_start_id := device.pending->>'id';
    end if;
    device.receipt := p_ack;
    device.pending := null;
  elsif (device.pending->>'expiresAt')::timestamptz <= now() then
    device.receipt := jsonb_build_object('id', device.pending->>'id', 'result', 'expired');
    device.pending := null;
  end if;

  for round_payload in select value from jsonb_array_elements(p_rounds) loop
    round_id := round_payload->>'id';
    candidate := nullif(round_payload->>'startId', '');
    if candidate is null and round_id = board_round_id then candidate := fallback_start_id; end if;
    confirmed_start := null;
    round_owner := null;
    round_versus := false;
    guest_key := null;
    if candidate is not null then
      update public.ishoot_start_tickets as ticket
         set round_id = round_payload->>'id'
       where ticket.id = candidate and ticket.player = round_payload->>'player'
         and ticket.game = round_payload->>'game'
         and (ticket.round_id is null or ticket.round_id = round_payload->>'id')
      returning ticket.id, ticket.owner_key, ticket.versus, ticket.guest_rank_key
        into confirmed_start, round_owner, round_versus, guest_key;
    end if;
    if round_owner is not null and round_id = board_round_id and round_payload->>'game' = 'memory' then
      p_board := jsonb_set(p_board, '{memoryOwner}', to_jsonb(round_owner));
    end if;
    if round_versus then
      if round_id = board_round_id then
        board_start_id := confirmed_start;
        p_board := jsonb_set(p_board, '{versusOwner}', to_jsonb(round_owner));
        p_board := jsonb_set(p_board, '{state,versus}', 'true'::jsonb);
      end if;
      continue;
    end if;
    select coalesce(sum((attempt->>0)::int), 0) into round_score
      from jsonb_array_elements(round_payload->'attempts') as t(attempt) where attempt->>0 = attempt->>1;
    if guest_key is null and candidate is null and round_payload->>'player' = 'Guest' then
      guest_key := 'cabinet:guest';
    end if;
    if guest_key is not null then
      if round_id = board_round_id then
        board_start_id := confirmed_start;
        p_board := jsonb_set(p_board, '{state,guestRound}', 'true'::jsonb);
      end if;
      if round_payload->>'status' = 'complete' and round_payload->>'game' = 'chase'
         and round_payload->>'type' = 'competition' then
        insert into public.ishoot_guest_scores as saved (round_id, participant_key, player, score, corrections)
          values (round_id, guest_key, round_payload->>'player', round_score,
                  coalesce((round_payload->>'corrections')::int, 0))
        on conflict on constraint ishoot_guest_scores_pkey do update
          set score = excluded.score, corrections = excluded.corrections
        where saved.participant_key = excluded.participant_key
          and saved.corrections < excluded.corrections and saved.score > excluded.score;
      end if;
      continue;
    end if;
    insert into public.ishoot_rounds as saved (id, player_key, status, payload, owner_key, start_id, score)
    values (round_id, lower(round_payload->>'player'), round_payload->>'status', round_payload,
      round_owner, confirmed_start, round_score)
    on conflict (id) do update set
      status = excluded.status, payload = excluded.payload, updated_at = now(), score = excluded.score,
      owner_key = coalesce(saved.owner_key, excluded.owner_key),
      start_id = coalesce(saved.start_id, excluded.start_id)
    where saved.player_key = excluded.player_key
      and coalesce((saved.payload->>'corrections')::int, 0) <= coalesce((excluded.payload->>'corrections')::int, 0)
      and (saved.status = 'active' or (saved.status = 'complete' and excluded.status = 'complete'
        and coalesce((saved.payload->>'corrections')::int, 0) < coalesce((excluded.payload->>'corrections')::int, 0)
        and saved.score > excluded.score))
      and (saved.payload is distinct from excluded.payload or (saved.owner_key is null and excluded.owner_key is not null))
      and jsonb_array_length(saved.payload->'attempts') <= jsonb_array_length(excluded.payload->'attempts');
  end loop;

  if board_start_id is null then
    select saved.start_id into board_start_id from public.ishoot_rounds as saved where saved.id = board_round_id;
  end if;
  if board_start_id is not null then
    p_board := jsonb_set(p_board, '{state,startId}', to_jsonb(board_start_id));
  end if;
  update public.ishoot_board set board = p_board, seen_at = now(), pending = device.pending, receipt = device.receipt
   where id = 1;
  return device.pending;
end;
$$;

revoke all on function public.ishoot_auth_budget(text, text), public.ishoot_chase_leaderboard()
  from public, anon, authenticated;
grant execute on function public.ishoot_auth_budget(text, text), public.ishoot_chase_leaderboard()
  to service_role;
notify pgrst, 'reload schema';
commit;
