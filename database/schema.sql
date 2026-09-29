-- IBVAP Supabase schema
-- Run this in the Supabase SQL editor of a new project.

create table if not exists cameras (
  id text primary key,               -- e.g. 'cam1', matches the id used in the camera URL
  label text not null default 'Unnamed camera',
  location text,
  is_active boolean not null default false,
  last_seen_at timestamptz
);

create table if not exists detections (
  id bigint generated always as identity primary key,
  camera_id text references cameras(id) on delete set null,
  detection_type text not null,      -- 'person' | 'vehicle' | 'face' | 'anpr' | etc
  confidence numeric,
  bounding_box jsonb,                -- {x, y, w, h} in pixels of the source frame
  snapshot_url text,                 -- optional: URL to a saved snapshot image
  created_at timestamptz not null default now()
);

create table if not exists alerts (
  id bigint generated always as identity primary key,
  detection_id bigint references detections(id) on delete cascade,
  severity text not null default 'info',   -- 'info' | 'warning' | 'critical'
  message text not null,
  acknowledged boolean not null default false,
  created_at timestamptz not null default now()
);

create index if not exists idx_detections_created_at on detections(created_at desc);
create index if not exists idx_detections_camera on detections(camera_id);

-- Allow anon key to read/write for demo purposes.
-- IMPORTANT: tighten these policies before any real deployment.
alter table cameras enable row level security;
alter table detections enable row level security;
alter table alerts enable row level security;

create policy "public read cameras" on cameras for select using (true);
create policy "public write cameras" on cameras for insert with check (true);
create policy "public update cameras" on cameras for update using (true);

create policy "public read detections" on detections for select using (true);
create policy "public write detections" on detections for insert with check (true);

create policy "public read alerts" on alerts for select using (true);
create policy "public write alerts" on alerts for insert with check (true);
create policy "public update alerts" on alerts for update using (true);
