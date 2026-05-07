export type EventPayload = string | Error | undefined;
export type EventCallback = (payload?: EventPayload) => void;

/**
 * A basic EventEmitter class for ArkTS.
 * Allows components to subscribe to events and instances to emit them.
 */
export class EventEmitter {
  private listeners: Map<string, EventCallback[]> = new Map();

  /**
   * Subscribe to an event.
   */
  public on(event: string, callback: EventCallback): void {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, []);
    }
    this.listeners.get(event)?.push(callback);
  }

  /**
   * Unsubscribe from an event.
   */
  public off(event: string, callback: EventCallback): void {
    const callbacks = this.listeners.get(event);
    if (callbacks) {
      this.listeners.set(event, callbacks.filter(cb => cb !== callback));
    }
  }

  /**
   * Emit an event, calling all subscribed callbacks.
   */
  protected emit(event: string, payload?: EventPayload): void {
    const callbacks = this.listeners.get(event);
    if (callbacks) {
      callbacks.forEach(callback => {
        try {
          callback(payload);
        } catch (e) {
          // Prevent one bad listener from breaking the emit chain
          console.error(`Error in event listener for ${event}:`, e);
        }
      });
    }
  }

  /**
   * Remove all listeners.
   */
  protected removeAllListeners(event?: string): void {
    if (event) {
      this.listeners.delete(event);
    } else {
      this.listeners.clear();
    }
  }
}
