import { createClient } from "@supabase/supabase-js";

// Fill these in from your Supabase project settings (Project Settings > API),
// or set VITE_SUPABASE_URL / VITE_SUPABASE_ANON_KEY in a .env file.
const SUPABASE_URL = import.meta.env.VITE_SUPABASE_URL || "https://ieqcvunksqqxqjfeslos.supabase.co";
const SUPABASE_ANON_KEY = import.meta.env.VITE_SUPABASE_ANON_KEY || "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImllcWN2dW5rc3FxeHFqZmVzbG9zIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTA0MjYwMjIsImV4cCI6MjEwNjAwMjAyMn0.-tHl27No5uvu0-Dt7IXAAOc-5aupKjfK_g2AevP7LPU";

export const supabase = createClient(SUPABASE_URL, SUPABASE_ANON_KEY);
