export type EventPayload = string | Error | undefined;
export type EventCallback = (payload?: EventPayload) => void;
