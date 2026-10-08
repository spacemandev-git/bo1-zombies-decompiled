// ratelimit.ts - a token bucket on an injected clock (milliseconds).

export class TokenBucket {
  private tokens: number;
  private last: number;

  constructor(
    readonly capacity: number,
    readonly perSecond: number,
    now: number,
  ) {
    this.tokens = capacity;
    this.last = now;
  }

  /** Takes `cost` tokens if there are enough; returns false (and takes nothing) otherwise. */
  take(now: number, cost = 1): boolean {
    if (now > this.last) {
      this.tokens = Math.min(this.capacity, this.tokens + ((now - this.last) * this.perSecond) / 1000);
      this.last = now;
    }
    if (this.tokens < cost) return false;
    this.tokens -= cost;
    return true;
  }
}
