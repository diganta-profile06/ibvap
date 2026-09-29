import { createClient } from "@supabase/supabase-js";

// Fill these in from your Supabase project settings (Project Settings > API),
// or set VITE_SUPABASE_URL / VITE_SUPABASE_ANON_KEY in a .env file.
const SUPABASE_URL = import.meta.env.VITE_SUPABASE_URL || "https://YOUR-PROJECT.supabase.co";
const SUPABASE_ANON_KEY = import.meta.env.VITE_SUPABASE_ANON_KEY || "YOUR-ANON-KEY";

export const supabase = createClient(SUPABASE_URL, SUPABASE_ANON_KEY);
